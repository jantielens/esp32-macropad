#pragma once

#include "board_config.h"

#if HAS_BOOT_LOGO

#include <stddef.h>
#include <stdint.h>

#define BOOT_LOGO_MAX_UPLOAD_BYTES (1024UL * 1024UL)
#define BOOT_LOGO_TEMP_PATH "/boot-logo/upload.tmp"

enum BootLogoPublishResult : uint8_t {
		BOOT_LOGO_PUBLISH_SUCCESS,
		BOOT_LOGO_PUBLISH_INVALID_IMAGE,
		BOOT_LOGO_PUBLISH_STORAGE_ERROR,
		BOOT_LOGO_PUBLISH_OTA_CONFLICT,
};

bool boot_logo_store_begin_upload();
void boot_logo_store_abort_upload();
BootLogoPublishResult boot_logo_store_publish_upload(uint8_t slot, const char** content_type);
bool boot_logo_store_slot_available(uint8_t slot);
bool boot_logo_store_slot_path(uint8_t slot, char* path, size_t path_len,
		const char** content_type);
bool boot_logo_store_delete_all();
bool boot_logo_store_load(uint8_t slot, uint32_t background_rgb);

#endif
