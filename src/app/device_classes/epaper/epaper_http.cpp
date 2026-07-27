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

namespace {

constexpr uint32_t kHttpTimeoutMs = 15000;
constexpr uint32_t kDownloadIdleTimeoutMs = 15000;

}  // namespace

bool epaper_http_read_body(HTTPClient& http, uint8_t** out_buf, size_t* out_len,
		size_t* body_bytes_read, bool honor_content_length) {
		*out_buf = nullptr;
		*out_len = 0;
		if (body_bytes_read) *body_bytes_read = 0;

		const int length_hint = honor_content_length ? http.getSize() : -1;
		WiFiClient* stream = http.getStreamPtr();
		size_t capacity = length_hint > 0 ? (size_t)length_hint : (size_t)(256 * 1024);
		uint8_t* buffer = (uint8_t*)heap_caps_malloc(
				capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (!buffer) {
				LOGW("Epaper", "image buffer alloc failed (%u bytes)", (unsigned)capacity);
				return false;
		}

		size_t total = 0;
		uint32_t last_progress_ms = millis();
		while ((http.connected() || stream->available()) &&
				(length_hint < 0 || total < (size_t)length_hint)) {
				size_t available = stream->available();
				if (available) {
						if (length_hint > 0) {
								const size_t remaining = (size_t)length_hint - total;
								if (available > remaining) available = remaining;
						}
						if (total + available > capacity) {
								size_t new_capacity = capacity * 2;
								while (total + available > new_capacity) new_capacity *= 2;
								uint8_t* grown = (uint8_t*)heap_caps_realloc(buffer, new_capacity,
										MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
								if (!grown) {
										heap_caps_free(buffer);
										return false;
								}
								buffer = grown;
								capacity = new_capacity;
						}
						const int count = stream->read(buffer + total, available);
						if (count <= 0) break;
						total += (size_t)count;
						if (body_bytes_read) *body_bytes_read += (size_t)count;
						last_progress_ms = millis();
				} else {
						if (length_hint > 0 && total >= (size_t)length_hint) break;
						if (millis() - last_progress_ms > kDownloadIdleTimeoutMs) {
								LOGW("Epaper", "image download stalled after %u bytes", (unsigned)total);
								heap_caps_free(buffer);
								return false;
						}
						delay(1);
				}
		}
		if (total < 4) {
				heap_caps_free(buffer);
				return false;
		}
		*out_buf = buffer;
		*out_len = total;
		return true;
}

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
		http.setTimeout(kHttpTimeoutMs);
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
			LOGW("Epaper", "image GET HTTP %d (%s)%s", code,
				http.errorToString(code).c_str(), hop ? " (redirect)" : "");
			http.end();
			return false;
		}

		const uint32_t t0 = millis();
		const int expected_length = http.getSize();
		size_t body_bytes_read = 0;
		const bool read_ok = epaper_http_read_body(http, out_buf, out_len, &body_bytes_read);

		// Protect the WiFi MAC DMA: brief pause after closing the TCP connection
		// (project convention, see image_fetch.cpp).
		http.end();
		delay(100);

		if (!read_ok) return false;
		if (expected_length > 0 && *out_len != (size_t)expected_length) {
				LOGW("Epaper", "image download incomplete (%u/%d bytes)",
						(unsigned)*out_len, expected_length);
				heap_caps_free(*out_buf);
				*out_buf = nullptr;
				*out_len = 0;
				return false;
		}
		LOGI("Epaper", "image downloaded %u bytes in %lu ms",
			 (unsigned)body_bytes_read, (unsigned long)(millis() - t0));
		return true;
	}

	return false;  // redirect loop exhausted without a 200
}

#endif // HAS_EPAPER
