#include "web_portal_ota.h"

#include "web_portal_auth.h"
#include "web_portal_firmware.h"
#include "web_portal_state.h"

#include "device_telemetry.h"
#include "log_manager.h"

#include <Update.h>

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

// OTA upload state gate (avoid concurrent uploads).
static portMUX_TYPE g_ota_upload_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t g_ota_last_percent = 0;

static size_t ota_progress = 0;
static size_t ota_total = 0;

// POST /api/update - Handle OTA firmware upload
void handleOTAUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		if (!portal_auth_gate(request)) return;

		// First chunk - initialize OTA
		if (index == 0) {
				// Guard against concurrent OTA uploads or online-update flow.
				bool allowed = false;
				portENTER_CRITICAL(&g_ota_upload_mux);
				if (!web_portal_ota_in_progress() && !web_portal_firmware_update_in_progress()) {
						web_portal_set_ota_in_progress(true);
						g_ota_last_percent = 0;
						allowed = true;
				}
				portEXIT_CRITICAL(&g_ota_upload_mux);

				if (!allowed) {
						request->send(409, "application/json", "{\"success\":false,\"message\":\"Update already in progress\"}");
						return;
				}

						LOGI("OTA", "Update start");
						LOGI("OTA", "File: %s", filename.c_str());
						LOGI("OTA", "Size: %d bytes", request->contentLength());

				ota_progress = 0;
				ota_total = request->contentLength();

				// Check if filename ends with .bin
				if (!filename.endsWith(".bin")) {
								LOGE("OTA", "Not a .bin file");
						request->send(400, "application/json", "{\"success\":false,\"message\":\"Only .bin files are supported\"}");
						portENTER_CRITICAL(&g_ota_upload_mux);
						web_portal_set_ota_in_progress(false);
						portEXIT_CRITICAL(&g_ota_upload_mux);
						return;
				}

				// Get OTA partition size
				size_t updateSize = (ota_total > 0) ? ota_total : UPDATE_SIZE_UNKNOWN;
				size_t freeSpace = device_telemetry_free_sketch_space();

						LOGI("OTA", "Free space: %d bytes", freeSpace);

				// Validate size before starting
				if (ota_total > 0 && ota_total > freeSpace) {
								LOGE("OTA", "Firmware too large");
						request->send(400, "application/json", "{\"success\":false,\"message\":\"Firmware too large\"}");
						portENTER_CRITICAL(&g_ota_upload_mux);
						web_portal_set_ota_in_progress(false);
						portEXIT_CRITICAL(&g_ota_upload_mux);
						return;
				}

				// Begin OTA update
				if (!Update.begin(updateSize, U_FLASH)) {
								LOGE("OTA", "Begin failed");
						Update.printError(Serial);
						request->send(500, "application/json", "{\"success\":false,\"message\":\"OTA begin failed\"}");
						portENTER_CRITICAL(&g_ota_upload_mux);
						web_portal_set_ota_in_progress(false);
						portEXIT_CRITICAL(&g_ota_upload_mux);
						return;
				}
		}

		// Write chunk to flash
		if (len) {
				if (Update.write(data, len) != len) {
								LOGE("OTA", "Write failed");
						Update.printError(Serial);
						Update.abort();
						request->send(500, "application/json", "{\"success\":false,\"message\":\"Write failed\"}");
						portENTER_CRITICAL(&g_ota_upload_mux);
						web_portal_set_ota_in_progress(false);
						portEXIT_CRITICAL(&g_ota_upload_mux);
						return;
				}

				ota_progress += len;

				// Print progress every 10%
				if (ota_total > 0) {
						uint8_t percent = (ota_progress * 100) / ota_total;
						if (percent >= (uint8_t)(g_ota_last_percent + 10)) {
										LOGI("OTA", "Progress: %d%%", percent);
								g_ota_last_percent = percent;
						}
				}
		}

		// Final chunk - complete OTA
		if (final) {
				if (Update.end(true)) {
								LOGI("OTA", "Written: %d bytes", ota_progress);
								LOGI("OTA", "Success - rebooting");

						request->send(200, "application/json", "{\"success\":true,\"message\":\"Update successful! Rebooting...\"}");

						delay(500);
						ESP.restart();
				} else {
								LOGE("OTA", "Update failed");
						Update.printError(Serial);
						request->send(500, "application/json", "{\"success\":false,\"message\":\"Update failed\"}");
				}

				portENTER_CRITICAL(&g_ota_upload_mux);
				web_portal_set_ota_in_progress(false);
				portEXIT_CRITICAL(&g_ota_upload_mux);
		}
}
