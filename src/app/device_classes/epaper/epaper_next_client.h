#pragma once

#ifndef EPAPER_NEXT_CLIENT_H
#define EPAPER_NEXT_CLIENT_H

#include "board_config.h"

#if HAS_EPAPER && defined(BOARD_RETERMINAL_E1003)

#include <stddef.h>
#include <stdint.h>

struct EpaperCurrentFingerprint {
		bool valid;
		char image_key[65];
		uint32_t content_crc32;
};

enum class EpaperNextResult : uint8_t {
		Show,
		Keep,
		AuthFailed,
		UnsupportedMajor,
		FailedFetch,
		FailedContent,
};

struct EpaperNextPayload {
		EpaperNextResult result;
		uint8_t* data;
		size_t len;
		uint8_t* prepared_data;
		size_t prepared_len;
		char image_key[65];
		char media_type[48];
		uint32_t content_crc32;
		bool from_cache;
		size_t body_bytes_read;
};

EpaperNextPayload epaper_next_client_fetch(const char* service_base,
		const char* bearer_token, const EpaperCurrentFingerprint& current,
		bool cache_enabled, uint8_t max_cycles = 2);
void epaper_next_payload_release(EpaperNextPayload* payload);

#endif

#endif // EPAPER_NEXT_CLIENT_H
