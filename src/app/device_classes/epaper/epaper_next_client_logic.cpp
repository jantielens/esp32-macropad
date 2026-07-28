#include "epaper_next_client_logic.h"

#include <string.h>

EpaperNextAction epaper_next_action_for_status(int status) {
		switch (status) {
				case 200: return EpaperNextAction::DownloadInline;
				case 204: return EpaperNextAction::Keep;
				case 302: return EpaperNextAction::FollowRedirect;
				case 401: return EpaperNextAction::AuthFailed;
				case 404: return EpaperNextAction::UnsupportedMajor;
				case 405: return EpaperNextAction::TransientFailure;
				default:
						if (status >= 500 && status <= 599) {
								return EpaperNextAction::TransientFailure;
						}
						return EpaperNextAction::InvalidResponse;
		}
}

EpaperRetryDecision epaper_next_retry_decision(EpaperNextResult result) {
		switch (result) {
				case EpaperNextResult::Show: return EpaperRetryDecision::Draw;
				case EpaperNextResult::Keep: return EpaperRetryDecision::Skip;
				default: return EpaperRetryDecision::Fail;
		}
}

bool epaper_source_uses_service(EpaperSourceMode mode) {
		return mode == EpaperSourceMode::Service;
}

bool epaper_source_advances_carousel(EpaperSourceMode mode, uint8_t carousel_count) {
		return !epaper_source_uses_service(mode) && carousel_count > 0;
}

uint32_t epaper_source_refresh_interval(EpaperSourceMode mode,
		uint32_t service_interval_seconds, uint32_t slot_interval_seconds) {
		return epaper_source_uses_service(mode)
				? service_interval_seconds : slot_interval_seconds;
}

bool epaper_next_use_cached_blob(bool cache_enabled, bool cache_valid) {
		return cache_enabled && cache_valid;
}

bool epaper_next_parse_crc32(const char* value, uint32_t* out_crc) {
		if (!value || !out_crc || strlen(value) != 8) return false;
		uint32_t parsed = 0;
		for (size_t index = 0; index < 8; ++index) {
				const char ch = value[index];
				uint8_t nibble = 0;
				if (ch >= '0' && ch <= '9') {
						nibble = (uint8_t)(ch - '0');
				} else if (ch >= 'a' && ch <= 'f') {
						nibble = (uint8_t)(ch - 'a' + 10);
				} else {
						return false;
				}
				parsed = (parsed << 4) | nibble;
		}
		*out_crc = parsed;
		return true;
}

bool epaper_next_valid_image_key(const char* value) {
		if (!value) return false;
		const size_t length = strlen(value);
		if (length == 0 || length > 64) return false;
		for (size_t index = 0; index < length; ++index) {
				const char ch = value[index];
				const bool valid = (ch >= 'a' && ch <= 'z') ||
						(ch >= 'A' && ch <= 'Z') ||
						(ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
				if (!valid) return false;
		}
		return true;
}
