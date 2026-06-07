// Shared HTTP(S) image downloader for the e-paper device class.
//
// Both the Inkplate (InkplateLibrary 3-bit) and Seeed reTerminal E1003
// (IT8951) drivers fetch a full image / transport blob over HTTP(S) into a
// PSRAM buffer following the same redirect + chunk-growth pattern. Keeping a
// single implementation here stops the two drivers from drifting apart.
// arduino-cli only compiles .cpp files in the sketch root, so this unit is
// #include-aggregated into epaper_device_class.cpp.

#include "board_config.h"

#if HAS_EPAPER

#include "device_classes/epaper/epaper_http.h"
#include "log_manager.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

bool epaper_http_download(const char* url, uint8_t** out_buf, size_t* out_len) {
	*out_buf = nullptr;
	*out_len = 0;

	const int kMaxRedirects = 3;
	String current = url;

	for (int hop = 0; hop <= kMaxRedirects; ++hop) {
		const bool is_https = current.startsWith("https://");

		HTTPClient http;
		WiFiClientSecure secure;
		WiFiClient plain;
		bool begin_ok = false;
		if (is_https) {
			secure.setInsecure();  // image integrity is the publisher's concern
			begin_ok = http.begin(secure, current);
		} else {
			begin_ok = http.begin(plain, current);
		}
		if (!begin_ok) {
			LOGW("Epaper", "image GET begin failed%s", hop ? " (redirect)" : "");
			return false;
		}
		http.setTimeout(8000);
		const char* collect[] = {"Location"};
		http.collectHeaders(collect, 1);

		const int code = http.GET();

		// 3xx: capture Location and retry against it (one TCP session per hop).
		if (code == HTTP_CODE_MOVED_PERMANENTLY || code == HTTP_CODE_FOUND ||
			code == HTTP_CODE_SEE_OTHER || code == HTTP_CODE_TEMPORARY_REDIRECT ||
			code == HTTP_CODE_PERMANENT_REDIRECT) {
			String loc = http.header("Location");
			http.end();
			if (loc.length() == 0) {
				LOGW("Epaper", "image GET %d with no Location header", code);
				return false;
			}
			if (hop == kMaxRedirects) {
				LOGW("Epaper", "image GET redirect limit reached");
				return false;
			}
			current = loc;
			delay(50);  // brief settle before reconnecting to the new host
			continue;
		}

		if (code != HTTP_CODE_OK) {
			LOGW("Epaper", "image GET HTTP %d%s", code, hop ? " (redirect)" : "");
			http.end();
			return false;
		}

		const int len = http.getSize();  // -1 when chunked / unknown
		WiFiClient* stream = http.getStreamPtr();

		size_t cap = (len > 0) ? (size_t)len : (size_t)(256 * 1024);
		uint8_t* buf = (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (!buf) {
			LOGW("Epaper", "image buffer alloc failed (%u bytes)", (unsigned)cap);
			http.end();
			return false;
		}

		size_t total = 0;
		bool stalled = false;
		const uint32_t t0 = millis();
		while (http.connected() && (len < 0 || total < (size_t)len)) {
			const size_t avail = stream->available();
			if (avail) {
				if (total + avail > cap) {
					// Chunked stream outgrew the estimate — grow the buffer.
					size_t new_cap = cap * 2;
					while (total + avail > new_cap) new_cap *= 2;
					uint8_t* grown = (uint8_t*)heap_caps_realloc(buf, new_cap,
						MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
					if (!grown) {
						LOGW("Epaper", "image buffer realloc failed (%u bytes)", (unsigned)new_cap);
						heap_caps_free(buf);
						http.end();
						return false;
					}
					buf = grown;
					cap = new_cap;
				}
				const int n = stream->readBytes(buf + total, avail);
				if (n <= 0) break;
				total += (size_t)n;
			} else {
				if (len > 0 && total >= (size_t)len) break;
				if (millis() - t0 > 15000) {
					LOGW("Epaper", "image download stalled after %u bytes", (unsigned)total);
					stalled = true;
					break;
				}
				delay(1);
			}
		}

		// Protect the WiFi MAC DMA: brief pause after closing the TCP connection
		// (project convention, see image_fetch.cpp).
		http.end();
		delay(100);

		if (stalled || total < 4) {
			LOGW("Epaper", "image download incomplete (%u bytes)", (unsigned)total);
			heap_caps_free(buf);
			return false;
		}
		LOGI("Epaper", "image downloaded %u bytes in %lu ms",
			 (unsigned)total, (unsigned long)(millis() - t0));
		*out_buf = buf;
		*out_len = total;
		return true;
	}

	return false;  // redirect loop exhausted without a 200
}

#endif // HAS_EPAPER
