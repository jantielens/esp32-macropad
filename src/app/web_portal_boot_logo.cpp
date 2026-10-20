#include "board_config.h"

#if HAS_BOOT_LOGO

#include "boot_logo_store.h"
#include "config_manager.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "storage.h"
#include "web_portal_auth.h"
#include "web_portal_json.h"
#include "web_portal_state.h"
#include "web_portal_boot_logo.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <new>
#include <string.h>

namespace {

struct BootLogoUpload {
		File file;
		size_t total = 0;
		size_t received = 0;
		uint8_t slot = CONFIG_BOOT_LOGO_SLOT_NONE;
		uint16_t error_status = 0;
		bool owns_upload = false;
		bool writing = false;
};

portMUX_TYPE g_boot_logo_upload_mux = portMUX_INITIALIZER_UNLOCKED;
bool g_boot_logo_upload_active = false;

bool claim_upload() {
		portENTER_CRITICAL(&g_boot_logo_upload_mux);
		const bool claimed = !g_boot_logo_upload_active;
		if (claimed) g_boot_logo_upload_active = true;
		portEXIT_CRITICAL(&g_boot_logo_upload_mux);
		return claimed;
}

void release_upload() {
		portENTER_CRITICAL(&g_boot_logo_upload_mux);
		g_boot_logo_upload_active = false;
		portEXIT_CRITICAL(&g_boot_logo_upload_mux);
}

void reset_upload(AsyncWebServerRequest* request, bool abort) {
		if (!request) return;
		auto* upload = static_cast<BootLogoUpload*>(request->_tempObject);
		if (!upload) return;
		request->_tempObject = nullptr;
		if (upload->file) upload->file.close();
		if (abort && upload->owns_upload) boot_logo_store_abort_upload();
		if (upload->owns_upload) release_upload();
		delete upload;
}

uint8_t inactive_slot(const DeviceConfig* config) {
		if (config && config->boot_logo_slot == 0) return 1;
		return 0;
}

bool parse_slot(AsyncWebServerRequest* request, uint8_t* slot) {
		if (!request || !slot || !request->hasParam("slot")) return false;
		const String value = request->getParam("slot")->value();
		if (value.length() != 1 || (value[0] != '0' && value[0] != '1')) return false;
		*slot = static_cast<uint8_t>(value[0] - '0');
		return true;
}

void send_upload_error(AsyncWebServerRequest* request, uint16_t status, const char* message) {
		web_portal_send_json_error(request, status, message);
}

void boot_logo_upload_disconnected(AsyncWebServerRequest* request) {
		reset_upload(request, true);
}

}  // namespace

void handleGetBootLogo(AsyncWebServerRequest* request) {
		if (!portal_auth_gate(request)) return;
		DeviceConfig* config = web_portal_get_current_config();
		if (!config) {
				send_upload_error(request, 503, "Configuration is unavailable");
				return;
		}
		char response[160];
		snprintf(response, sizeof(response),
				"{\"active_slot\":%u,\"slot0_available\":%s,\"slot1_available\":%s}",
				config->boot_logo_slot,
				boot_logo_store_slot_available(0) ? "true" : "false",
				boot_logo_store_slot_available(1) ? "true" : "false");
		request->send(200, "application/json", response);
}

void handleDeleteBootLogo(AsyncWebServerRequest* request) {
		if (!portal_auth_gate(request)) return;
		if (ota_activity_is_active()) {
				send_upload_error(request, 409, "Firmware update is active");
				return;
		}
		if (!claim_upload()) {
				send_upload_error(request, 409, "A logo upload is in progress");
				return;
		}
		DeviceConfig* config = web_portal_get_current_config();
		if (!config) {
				release_upload();
				send_upload_error(request, 503, "Configuration is unavailable");
				return;
		}
		if (config->boot_logo_slot != CONFIG_BOOT_LOGO_SLOT_NONE) {
				release_upload();
				send_upload_error(request, 409, "Select the device logo before deleting uploaded logos");
				return;
		}
		const bool deleted = boot_logo_store_delete_all();
		release_upload();
		if (!deleted) {
				const bool ota_conflict = ota_activity_is_active();
				send_upload_error(request, ota_conflict ? 409 : 507,
						ota_conflict ? "Firmware update is active" : "Could not delete all uploaded boot logos");
				return;
		}
		request->send(200, "application/json", "{\"success\":true}");
}

void handleGetBootLogoThumbnail(AsyncWebServerRequest* request) {
		if (!portal_auth_gate(request)) return;
		uint8_t slot = 0;
		if (!parse_slot(request, &slot)) {
				send_upload_error(request, 400, "Slot must be 0 or 1");
				return;
		}
		char path[40] = {};
		const char* content_type = nullptr;
		if (!boot_logo_store_slot_path(slot, path, sizeof(path), &content_type)) {
				send_upload_error(request, 404, "Logo preview is unavailable");
				return;
		}
		request->send(Storage, path, content_type);
}

void handlePostBootLogoUpload(AsyncWebServerRequest* request, uint8_t* data,
		size_t length, size_t index, size_t total) {
		if (!portal_auth_gate(request)) return;
		auto* upload = static_cast<BootLogoUpload*>(request->_tempObject);
		if (index == 0 && !upload) {
				upload = new (std::nothrow) BootLogoUpload;
				if (!upload) return;
				request->_tempObject = upload;
				request->onDisconnect([request]() { boot_logo_upload_disconnected(request); });

				if (!total || total > BOOT_LOGO_MAX_UPLOAD_BYTES) {
						upload->error_status = 413;
				} else if (!claim_upload()) {
						upload->error_status = 409;
				} else {
						upload->owns_upload = true;
						if (ota_activity_is_active()) {
								upload->error_status = 409;
						} else if (!boot_logo_store_begin_upload()) {
								upload->error_status = 507;
						} else {
								upload->file = Storage.open(BOOT_LOGO_TEMP_PATH, "w");
								if (!upload->file) upload->error_status = 507;
								else {
										upload->total = total;
										DeviceConfig* config = web_portal_get_current_config();
										if (!config) upload->error_status = 503;
										else {
												upload->slot = inactive_slot(config);
												upload->writing = true;
										}
								}
						}
				}
		}

		if (!upload || upload->error_status || !upload->writing) return;
		if (ota_activity_is_active()) {
				upload->error_status = 409;
				return;
		}
		if (total != upload->total || index != upload->received ||
				index > total || length > total - index || (length && !data)) {
				upload->error_status = 400;
				return;
		}
		if (length && upload->file.write(data, length) != length) {
				upload->error_status = 507;
				return;
		}
		upload->received += length;
}

void handlePostBootLogoUploadComplete(AsyncWebServerRequest* request) {
		if (!portal_auth_gate(request)) return;
		auto* upload = static_cast<BootLogoUpload*>(request->_tempObject);
		if (!upload) {
				send_upload_error(request, 503, "Upload could not be initialized");
				return;
		}
		if (upload->error_status || !upload->writing || upload->received != upload->total) {
				const uint16_t status = upload->error_status ? upload->error_status : 400;
				const char* message = status == 413 ? "Image is empty or exceeds 1 MiB" :
						status == 409 ? "Upload conflicts with another operation or firmware update" :
						status == 507 ? "Unable to write the uploaded image" :
						status == 503 ? "Configuration is unavailable" : "Upload framing is invalid";
				reset_upload(request, true);
				send_upload_error(request, status, message);
				return;
		}

		upload->file.close();
		if (ota_activity_is_active()) {
				reset_upload(request, true);
				send_upload_error(request, 409, "Firmware update is active");
				return;
		}
		DeviceConfig* config = web_portal_get_current_config();
		if (!config) {
				reset_upload(request, true);
				send_upload_error(request, 503, "Configuration is unavailable");
				return;
		}
		if (config->boot_logo_slot == upload->slot) upload->slot = upload->slot == 0 ? 1 : 0;
		const BootLogoPublishResult publish_result =
				boot_logo_store_publish_upload(upload->slot, nullptr);
		if (publish_result != BOOT_LOGO_PUBLISH_SUCCESS) {
				reset_upload(request, true);
				const uint16_t status = publish_result == BOOT_LOGO_PUBLISH_INVALID_IMAGE ? 400 :
						publish_result == BOOT_LOGO_PUBLISH_OTA_CONFLICT ? 409 : 507;
				const char* message = publish_result == BOOT_LOGO_PUBLISH_INVALID_IMAGE
						? "Image is invalid or unsupported. Use PNG or baseline (non-progressive) JPEG, up to 1 MiB and 1,000,000 pixels" :
						publish_result == BOOT_LOGO_PUBLISH_OTA_CONFLICT
						? "Firmware update is active" : "Unable to store the uploaded image";
				send_upload_error(request, status, message);
				return;
		}
		const uint8_t slot = upload->slot;
		reset_upload(request, false);
		char response[80];
		snprintf(response, sizeof(response), "{\"success\":true,\"slot\":%u}", slot);
		request->send(200, "application/json", response);
}

#endif
