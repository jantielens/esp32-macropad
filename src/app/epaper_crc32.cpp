#include "epaper_crc32.h"

#if HAS_EPAPER

#include "log_manager.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static const uint16_t kBackoffMs[] = {300, 700, 1500};
static const uint8_t kMaxAttempts = sizeof(kBackoffMs) / sizeof(kBackoffMs[0]);

static uint32_t parse_crc_body(const String& body) {
		String trimmed = body;
		trimmed.trim();
		if (trimmed.length() == 0) return 0;

		// Accept "0xDEADBEEF", "DEADBEEF", or decimal.
		const char* c = trimmed.c_str();
		uint32_t value = 0;
		bool ok = false;

		if (trimmed.startsWith("0x") || trimmed.startsWith("0X")) {
				value = (uint32_t)strtoul(c + 2, nullptr, 16);
				ok = (value != 0 || strcmp(c + 2, "0") == 0);
		} else if (trimmed.length() == 8 && strspn(c, "0123456789abcdefABCDEF") == 8) {
				value = (uint32_t)strtoul(c, nullptr, 16);
				ok = true;
		} else {
				value = (uint32_t)strtoul(c, nullptr, 10);
				ok = (value != 0 || strcmp(c, "0") == 0);
		}

		return ok ? value : 0;
}

EpaperCrcFetchResult epaper_crc32_fetch_sidecar(const char* image_url) {
		EpaperCrcFetchResult out = {0, 0};
		if (!image_url || !*image_url) return out;

		String sidecar_url = String(image_url) + ".crc32";
		const bool is_https = sidecar_url.startsWith("https://");

		for (uint8_t attempt = 0; attempt < kMaxAttempts; attempt++) {
				HTTPClient http;
				bool begin_ok = false;
				int code = 0;

				if (is_https) {
						WiFiClientSecure client;
						client.setInsecure(); // sidecar is integrity-checked by CRC, not TLS
						begin_ok = http.begin(client, sidecar_url);
						if (begin_ok) {
								http.setTimeout(5000);
								code = http.GET();
								out.http_status = (int16_t)code;
								if (code == HTTP_CODE_OK) {
										out.crc = parse_crc_body(http.getString());
										http.end();
										LOGI("Epaper", "Sidecar CRC=%08x (HTTP %d, attempt %u)", (unsigned)out.crc, code, (unsigned)(attempt + 1));
										return out;
								}
								LOGW("Epaper", "Sidecar HTTP %d (attempt %u)", code, (unsigned)(attempt + 1));
								http.end();
						}
				} else {
						begin_ok = http.begin(sidecar_url);
						if (begin_ok) {
								http.setTimeout(5000);
								code = http.GET();
								out.http_status = (int16_t)code;
								if (code == HTTP_CODE_OK) {
										out.crc = parse_crc_body(http.getString());
										http.end();
										LOGI("Epaper", "Sidecar CRC=%08x (HTTP %d, attempt %u)", (unsigned)out.crc, code, (unsigned)(attempt + 1));
										return out;
								}
								LOGW("Epaper", "Sidecar HTTP %d (attempt %u)", code, (unsigned)(attempt + 1));
								http.end();
						}
				}

				if (!begin_ok) {
						out.http_status = 0;
						LOGW("Epaper", "Sidecar begin failed (attempt %u)", (unsigned)(attempt + 1));
				}

				if (attempt + 1 < kMaxAttempts) {
						delay(kBackoffMs[attempt]);
				}
		}

		return out;
}

#endif // HAS_EPAPER
