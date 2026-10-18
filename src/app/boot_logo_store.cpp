#include "boot_logo_store.h"

#if HAS_BOOT_LOGO

#include "display_manager.h"
#include "image_decoder.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "storage.h"

#include <esp_heap_caps.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace {

constexpr char kDirectory[] = "/boot-logo";
bool ensure_directory() {
		if (!storage_mount()) {
				LOGE("BootLogo", "Storage is unavailable");
				return false;
		}
		if (Storage.exists(kDirectory)) return true;
		if (!Storage.mkdir(kDirectory)) {
				LOGE("BootLogo", "Cannot create %s", kDirectory);
				return false;
		}
		return true;
}

bool read_source_file(const char* path, uint8_t** data, size_t* length) {
		if (!data || !length || !path || ota_activity_is_active() || !ensure_directory()) return false;
		*data = nullptr;
		*length = 0;

		File file = Storage.open(path, "r");
		if (!file || file.isDirectory()) {
				if (file) file.close();
				LOGW("BootLogo", "Cannot open %.80s", path);
				return false;
		}
		const size_t size = file.size();
		if (!size || size > BOOT_LOGO_MAX_UPLOAD_BYTES) {
				file.close();
				LOGW("BootLogo", "Invalid file size %u", static_cast<unsigned>(size));
				return false;
		}

		uint8_t* buffer = static_cast<uint8_t*>(
				heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
		if (!buffer) buffer = static_cast<uint8_t*>(malloc(size));
		if (!buffer) {
				file.close();
				LOGE("BootLogo", "Out of memory for %u-byte image", static_cast<unsigned>(size));
				return false;
		}

		size_t received = 0;
		while (received < size && !ota_activity_is_active()) {
				const size_t chunk = size - received < 4096 ? size - received : 4096;
				const int count = file.read(buffer + received, chunk);
				if (count <= 0) break;
				received += static_cast<size_t>(count);
				taskYIELD();
		}
		file.close();
		if (received != size || ota_activity_is_active()) {
				heap_caps_free(buffer);
				LOGW("BootLogo", "Image read was interrupted");
				return false;
		}
		*data = buffer;
		*length = size;
		return true;
}

bool slot_paths(uint8_t slot, char* png, size_t png_len, char* jpeg, size_t jpeg_len) {
		if (slot > 1 || !png || !png_len || !jpeg || !jpeg_len) return false;
		const int png_written = snprintf(png, png_len, "%s/slot%u.png", kDirectory, slot);
		const int jpeg_written = snprintf(jpeg, jpeg_len, "%s/slot%u.jpg", kDirectory, slot);
		return png_written > 0 && static_cast<size_t>(png_written) < png_len &&
				jpeg_written > 0 && static_cast<size_t>(jpeg_written) < jpeg_len;
}

uint16_t rgb888_to_rgb565(uint32_t rgb) {
		return static_cast<uint16_t>(
				(((rgb >> 19) & 0x1F) << 11) |
				(((rgb >> 10) & 0x3F) << 5) |
				((rgb >> 3) & 0x1F));
}

bool validate_source(const uint8_t* data, size_t length, uint32_t background_rgb,
		ImageFormat* format) {
		if (!data || !length || !format || ota_activity_is_active()) return false;
		*format = image_detect_format(data, length);
		if (*format != IMAGE_FORMAT_PNG && *format != IMAGE_FORMAT_JPEG) return false;
		uint16_t max_width = 0;
		uint16_t max_height = 0;
		if (!display_manager_get_splash_logo_bounds(&max_width, &max_height)) {
				LOGE("BootLogo", "Splash logo dimensions are unavailable");
				return false;
		}
		uint16_t* pixels = nullptr;
		size_t pixel_size = 0;
		uint16_t width = 0;
		uint16_t height = 0;
		const bool decoded = image_decode_boot_logo(
				data, length, max_width, max_height, rgb888_to_rgb565(background_rgb),
				&pixels, &pixel_size, &width, &height);
		if (pixels) heap_caps_free(pixels);
		return decoded && pixel_size == static_cast<size_t>(width) * height * sizeof(uint16_t);
}

}  // namespace

bool boot_logo_store_begin_upload() {
		if (ota_activity_is_active()) {
				LOGW("BootLogo", "Upload rejected while firmware update is active");
				return false;
		}
		if (!ensure_directory()) return false;
		if (Storage.exists(BOOT_LOGO_TEMP_PATH) && !Storage.remove(BOOT_LOGO_TEMP_PATH)) {
				LOGE("BootLogo", "Cannot clear previous upload");
				return false;
		}
		return true;
}

void boot_logo_store_abort_upload() {
		if (Storage.exists(BOOT_LOGO_TEMP_PATH) && !Storage.remove(BOOT_LOGO_TEMP_PATH)) {
				LOGW("BootLogo", "Cannot remove incomplete upload");
		}
}

BootLogoPublishResult boot_logo_store_publish_upload(uint8_t slot, const char** content_type) {
		if (content_type) *content_type = nullptr;
		if (ota_activity_is_active()) return BOOT_LOGO_PUBLISH_OTA_CONFLICT;
		if (slot > 1) return BOOT_LOGO_PUBLISH_STORAGE_ERROR;

		uint8_t* source = nullptr;
		size_t source_length = 0;
		if (!read_source_file(BOOT_LOGO_TEMP_PATH, &source, &source_length)) {
				return ota_activity_is_active()
						? BOOT_LOGO_PUBLISH_OTA_CONFLICT : BOOT_LOGO_PUBLISH_STORAGE_ERROR;
		}

		ImageFormat format = IMAGE_FORMAT_UNKNOWN;
		const bool valid = validate_source(source, source_length,
				BOOT_APPEARANCE_DEFAULT_BACKGROUND_RGB, &format);
		heap_caps_free(source);
		if (ota_activity_is_active()) {
				boot_logo_store_abort_upload();
				return BOOT_LOGO_PUBLISH_OTA_CONFLICT;
		}
		if (!valid) {
				LOGW("BootLogo", "Uploaded image is invalid or could not be decoded");
				boot_logo_store_abort_upload();
				return BOOT_LOGO_PUBLISH_INVALID_IMAGE;
		}

		char png_path[40] = {};
		char jpeg_path[40] = {};
		if (!slot_paths(slot, png_path, sizeof(png_path), jpeg_path, sizeof(jpeg_path))) {
				boot_logo_store_abort_upload();
				return BOOT_LOGO_PUBLISH_STORAGE_ERROR;
		}
		const char* destination = format == IMAGE_FORMAT_PNG ? png_path : jpeg_path;
		const char* obsolete = format == IMAGE_FORMAT_PNG ? jpeg_path : png_path;
		if (Storage.exists(obsolete) && !Storage.remove(obsolete)) {
				LOGE("BootLogo", "Cannot remove obsolete inactive slot file");
				boot_logo_store_abort_upload();
				return BOOT_LOGO_PUBLISH_STORAGE_ERROR;
		}
		if (Storage.exists(destination) && !Storage.remove(destination)) {
				LOGE("BootLogo", "Cannot replace inactive slot file");
				boot_logo_store_abort_upload();
				return BOOT_LOGO_PUBLISH_STORAGE_ERROR;
		}
		if (!Storage.rename(BOOT_LOGO_TEMP_PATH, destination)) {
				LOGE("BootLogo", "Cannot publish validated image to slot %u", slot);
				boot_logo_store_abort_upload();
				return BOOT_LOGO_PUBLISH_STORAGE_ERROR;
		}
		if (content_type) *content_type = format == IMAGE_FORMAT_PNG ? "image/png" : "image/jpeg";
		LOGI("BootLogo", "Validated image stored in inactive slot %u", slot);
		return BOOT_LOGO_PUBLISH_SUCCESS;
}

bool boot_logo_store_slot_path(uint8_t slot, char* path, size_t path_len,
		const char** content_type) {
		if (content_type) *content_type = nullptr;
		if (!path || !path_len || !ensure_directory()) return false;
		char png_path[40] = {};
		char jpeg_path[40] = {};
		if (!slot_paths(slot, png_path, sizeof(png_path), jpeg_path, sizeof(jpeg_path))) return false;
		const char* selected = Storage.exists(png_path) ? png_path :
				Storage.exists(jpeg_path) ? jpeg_path : nullptr;
		if (!selected) return false;
		const int written = snprintf(path, path_len, "%s", selected);
		if (written <= 0 || static_cast<size_t>(written) >= path_len) return false;
		if (content_type) *content_type = selected == png_path ? "image/png" : "image/jpeg";
		return true;
}

bool boot_logo_store_slot_available(uint8_t slot) {
		char path[40] = {};
		const char* content_type = nullptr;
		return boot_logo_store_slot_path(slot, path, sizeof(path), &content_type);
}

bool boot_logo_store_delete_all() {
		if (ota_activity_is_active() || !ensure_directory()) return false;
		bool success = true;
		for (uint8_t slot = 0; slot < 2; ++slot) {
				char png_path[40] = {};
				char jpeg_path[40] = {};
				if (!slot_paths(slot, png_path, sizeof(png_path), jpeg_path, sizeof(jpeg_path))) {
						return false;
				}
				const char* paths[] = {png_path, jpeg_path};
				for (const char* path : paths) {
						if (ota_activity_is_active()) return false;
						if (Storage.exists(path) && !Storage.remove(path)) {
								LOGE("BootLogo", "Cannot delete %s", path);
								success = false;
						}
				}
		}
		if (ota_activity_is_active()) return false;
		if (Storage.exists(BOOT_LOGO_TEMP_PATH) && !Storage.remove(BOOT_LOGO_TEMP_PATH)) {
				LOGE("BootLogo", "Cannot delete incomplete upload");
				success = false;
		}
		return success;
}

bool boot_logo_store_load(uint8_t slot, uint32_t background_rgb) {
		if (slot > 1 || ota_activity_is_active()) return false;
		char path[40] = {};
		const char* content_type = nullptr;
		if (!boot_logo_store_slot_path(slot, path, sizeof(path), &content_type)) {
				LOGW("BootLogo", "Selected slot %u is missing", slot);
				return false;
		}
		(void)content_type;
		uint8_t* source = nullptr;
		size_t source_length = 0;
		if (!read_source_file(path, &source, &source_length)) return false;

		uint16_t max_width = 0;
		uint16_t max_height = 0;
		uint16_t* pixels = nullptr;
		size_t pixel_size = 0;
		uint16_t width = 0;
		uint16_t height = 0;
		const bool decoded = display_manager_get_splash_logo_bounds(&max_width, &max_height) &&
				!ota_activity_is_active() &&
				image_decode_boot_logo(source, source_length, max_width, max_height,
						rgb888_to_rgb565(background_rgb), &pixels, &pixel_size, &width, &height);
		heap_caps_free(source);
		if (!decoded || !pixels ||
				pixel_size != static_cast<size_t>(width) * height * sizeof(uint16_t)) {
				if (pixels) heap_caps_free(pixels);
				LOGW("BootLogo", "Could not decode selected logo");
				return false;
		}
		if (!display_manager_set_splash_logo(pixels, width, height)) {
				LOGW("BootLogo", "Could not attach selected logo to splash");
				return false;
		}
		return true;
}

#endif
