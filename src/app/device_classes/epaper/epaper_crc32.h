#pragma once

#include "board_config.h"

#if HAS_EPAPER

#include <stdint.h>

struct EpaperCrcFetchResult {
		uint32_t crc;          // Parsed CRC value (0 when unavailable / invalid)
		int16_t http_status;   // HTTP status from the final attempt (0 = transport/begin failure)
		uint8_t attempts;      // Number of HTTP attempts made (1..3); useful for diagnosing slow servers
};

// Fetches "<image_url>.crc32" — a small text sidecar containing a single
// 32-bit unsigned integer in hex or decimal. Used to skip an expensive panel
// refresh when the upstream image has not changed since the previous wake.
//
// Returns 0 on any failure (HTTP error, parse error, empty body). Callers
// treat 0 as "unknown" and proceed with a refresh.
//
// Retries up to 3 times with 300 / 700 / 1500 ms backoff.
EpaperCrcFetchResult epaper_crc32_fetch_sidecar(const char* image_url);

#endif // HAS_EPAPER
