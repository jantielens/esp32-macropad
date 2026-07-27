#pragma once

#ifndef EPAPER_NEXT_CLIENT_LOGIC_H
#define EPAPER_NEXT_CLIENT_LOGIC_H

#include <stddef.h>
#include <stdint.h>

enum class EpaperSourceMode : uint8_t {
		SlotCarousel = 0,
		Service = 1,
};

enum class EpaperNextAction : uint8_t {
		DownloadInline,
		FollowRedirect,
		Keep,
		AuthFailed,
		UnsupportedMajor,
		TransientFailure,
		InvalidResponse,
};

EpaperNextAction epaper_next_action_for_status(int status);
bool epaper_source_uses_service(EpaperSourceMode mode);
bool epaper_source_advances_carousel(EpaperSourceMode mode, uint8_t carousel_count);
uint32_t epaper_source_refresh_interval(EpaperSourceMode mode,
		uint32_t service_interval_seconds, uint32_t slot_interval_seconds);
bool epaper_next_use_cached_blob(bool cache_enabled, bool cache_valid);
bool epaper_next_parse_crc32(const char* value, uint32_t* out_crc);
bool epaper_next_valid_image_key(const char* value);

#endif // EPAPER_NEXT_CLIENT_LOGIC_H
