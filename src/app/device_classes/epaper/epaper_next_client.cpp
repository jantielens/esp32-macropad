#include "board_config.h"

#if HAS_EPAPER && defined(BOARD_RETERMINAL_E1003)

#include "device_classes/epaper/epaper_next_client.h"

#include "device_classes/epaper/epaper_driver.h"
#include "device_classes/epaper/epaper_http.h"
#include "device_classes/epaper/epaper_next_client_logic.h"
#include "device_classes/epaper/epaper_sd_cache.h"
#include "device_classes/epaper/epaper_timing.h"
#include "device_classes/epaper/epaper_transport_crc32.h"
#include "log_manager.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace {

constexpr uint32_t kServiceHttpTimeoutMs = 15000;
constexpr const char* kImageKeyHeader = "Photoframe-Image-Key";
constexpr const char* kContentCrcHeader = "Photoframe-Content-CRC32";

struct ShowMetadata {
		char image_key[65];
		uint32_t content_crc32;
};

EpaperNextPayload empty_payload(EpaperNextResult result) {
		EpaperNextPayload payload = {};
		payload.result = result;
		return payload;
}

String next_url(const char* service_base) {
		String url = service_base ? service_base : "";
		while (url.endsWith("/")) url.remove(url.length() - 1);
		url += "/api/v1/next";
		return url;
}

bool begin_request(HTTPClient& http, WiFiClient& plain, WiFiClientSecure& secure,
		const String& url) {
		http.useHTTP10(true);
		if (url.startsWith("https://")) {
				secure.setInsecure();
				return http.begin(secure, url);
		}
		return http.begin(plain, url);
}

void collect_service_headers(HTTPClient& http) {
		const char* headers[] = {
				kImageKeyHeader,
				kContentCrcHeader,
				"Content-Type",
				"Content-Encoding",
				"Location",
				"WWW-Authenticate",
		};
		http.collectHeaders(headers, sizeof(headers) / sizeof(headers[0]));
}

bool parse_show_metadata(HTTPClient& http, ShowMetadata* metadata) {
		const String image_key = http.header(kImageKeyHeader);
		const String crc_text = http.header(kContentCrcHeader);
		if (!epaper_next_valid_image_key(image_key.c_str()) ||
				!epaper_next_parse_crc32(crc_text.c_str(), &metadata->content_crc32)) {
				return false;
		}
		strlcpy(metadata->image_key, image_key.c_str(), sizeof(metadata->image_key));
		return true;
}

bool valid_content_headers(HTTPClient& http, char* media_type, size_t media_type_size) {
		const String encoding = http.header("Content-Encoding");
		if (encoding.length() > 0 && !encoding.equalsIgnoreCase("identity")) return false;
		const String type = http.header("Content-Type");
		const bool supported = type == "image/jpeg" ||
				type == "application/vnd.photoframe.g16p" ||
				type == "application/vnd.photoframe.g16z";
		if (!supported) return false;
		strlcpy(media_type, type.c_str(), media_type_size);
		return true;
}

bool try_cache(uint32_t content_crc32, const char* media_type,
		EpaperNextPayload* payload) {
		const uint32_t started = millis();
		uint8_t* data = nullptr;
		size_t len = 0;
		if (!epaper_sd_cache_read(content_crc32, &data, &len)) return false;
		const bool valid = epaper_transport_crc32(data, len) == content_crc32 &&
				epaper_driver_prepare_service_blob(data, len, media_type,
						&payload->prepared_data, &payload->prepared_len);
		if (!valid) {
			heap_caps_free(data);
			epaper_sd_cache_remove(content_crc32);
			LOGW("Epaper", "Service cache entry failed revalidation; evicted");
			return false;
		}
		payload->data = data;
		payload->len = len;
		payload->from_cache = true;
		payload->body_bytes_read = 0;
		epaper_timing_set_fetch(millis() - started, true);
		return true;
}

bool read_and_validate_body(HTTPClient& http, const ShowMetadata& metadata,
		const char* media_type, EpaperNextPayload* payload) {
		const uint32_t started = millis();
		uint8_t* data = nullptr;
		size_t len = 0;
		size_t body_bytes_read = 0;
		if (!epaper_http_read_body(http, &data, &len, &body_bytes_read, false)) return false;
		payload->body_bytes_read = body_bytes_read;
		if (epaper_transport_crc32(data, len) != metadata.content_crc32 ||
				!epaper_driver_prepare_service_blob(data, len, media_type,
						&payload->prepared_data, &payload->prepared_len)) {
			heap_caps_free(data);
			return false;
		}
		payload->data = data;
		payload->len = len;
		epaper_timing_set_fetch(millis() - started, false);
		return true;
}

EpaperNextPayload follow_redirect(const String& location, const ShowMetadata& metadata,
		bool cache_enabled) {
		EpaperNextPayload payload = empty_payload(EpaperNextResult::FailedFetch);
		strlcpy(payload.image_key, metadata.image_key, sizeof(payload.image_key));
		payload.content_crc32 = metadata.content_crc32;

		WiFiClient plain;
		WiFiClientSecure secure;
		HTTPClient http;
		if (!begin_request(http, plain, secure, location)) return payload;
		http.setTimeout(kServiceHttpTimeoutMs);
		collect_service_headers(http);
		const int status = http.GET();
		if (status != HTTP_CODE_OK || !valid_content_headers(
				http, payload.media_type, sizeof(payload.media_type))) {
			http.end();
			return payload;
		}
		if (cache_enabled && try_cache(
				metadata.content_crc32, payload.media_type, &payload)) {
			http.end();
			payload.result = EpaperNextResult::Show;
			LOGI("Epaper", "Service redirect cache hit; body_bytes_read=0");
			return payload;
		}
		const bool ok = read_and_validate_body(http, metadata, payload.media_type, &payload);
		http.end();
		delay(100);
		payload.result = ok ? EpaperNextResult::Show : EpaperNextResult::FailedContent;
		return payload;
}

} // namespace

EpaperNextPayload epaper_next_client_fetch(const char* service_base,
		const char* bearer_token, const EpaperCurrentFingerprint& current,
		bool cache_enabled, uint8_t max_cycles) {
		if (!service_base || !*service_base || !bearer_token || !*bearer_token) {
				return empty_payload(EpaperNextResult::FailedFetch);
		}
		if (max_cycles == 0) return empty_payload(EpaperNextResult::FailedFetch);
		if (max_cycles > 2) max_cycles = 2;
		const String url = next_url(service_base);
		for (uint8_t attempt = 0; attempt < max_cycles; ++attempt) {
				WiFiClient plain;
				WiFiClientSecure secure;
				HTTPClient http;
				if (!begin_request(http, plain, secure, url)) {
						if (attempt + 1 < max_cycles) continue;
						return empty_payload(EpaperNextResult::FailedFetch);
				}
				http.setTimeout(kServiceHttpTimeoutMs);
				collect_service_headers(http);
				http.addHeader("Authorization", String("Bearer ") + bearer_token);
				if (current.valid) {
						char crc_text[9];
						snprintf(crc_text, sizeof(crc_text), "%08lx",
								(unsigned long)current.content_crc32);
						http.addHeader("Photoframe-Current-Image-Key", current.image_key);
						http.addHeader("Photoframe-Current-Content-CRC32", crc_text);
				}

				const int status = http.GET();
				const EpaperNextAction action = epaper_next_action_for_status(status);
				if (action == EpaperNextAction::Keep) {
						http.end();
						return empty_payload(EpaperNextResult::Keep);
				}
				if (action == EpaperNextAction::AuthFailed) {
						const bool bearer_challenge = http.header("WWW-Authenticate") == "Bearer";
						http.end();
						LOGW("Epaper", "Service authentication failed%s",
								bearer_challenge ? "" : " (missing Bearer challenge)");
						return empty_payload(EpaperNextResult::AuthFailed);
				}
				if (action == EpaperNextAction::UnsupportedMajor) {
						http.end();
						return empty_payload(EpaperNextResult::UnsupportedMajor);
				}
				if (action == EpaperNextAction::TransientFailure) {
						http.end();
						if (attempt + 1 < max_cycles) continue;
						return empty_payload(EpaperNextResult::FailedFetch);
				}
				if (action != EpaperNextAction::DownloadInline &&
						action != EpaperNextAction::FollowRedirect) {
						http.end();
						return empty_payload(EpaperNextResult::FailedFetch);
				}

				ShowMetadata metadata = {};
				if (!parse_show_metadata(http, &metadata)) {
						http.end();
						if (attempt + 1 < max_cycles) continue;
						return empty_payload(EpaperNextResult::FailedContent);
				}
				if (action == EpaperNextAction::FollowRedirect) {
						const String location = http.header("Location");
						http.end();
						if (location.length() == 0) return empty_payload(EpaperNextResult::FailedFetch);
						return follow_redirect(location, metadata, cache_enabled);
				}

				EpaperNextPayload payload = empty_payload(EpaperNextResult::FailedContent);
				strlcpy(payload.image_key, metadata.image_key, sizeof(payload.image_key));
				payload.content_crc32 = metadata.content_crc32;
				if (!valid_content_headers(http, payload.media_type, sizeof(payload.media_type))) {
						http.end();
						if (attempt + 1 < max_cycles) continue;
						return payload;
				}
				if (cache_enabled && try_cache(metadata.content_crc32, payload.media_type, &payload)) {
						http.end();
						payload.result = EpaperNextResult::Show;
						LOGI("Epaper", "Service cache hit; body_bytes_read=0");
						return payload;
				}
				const bool valid = read_and_validate_body(http, metadata, payload.media_type, &payload);
				http.end();
				delay(100);
				LOGI("Epaper", "Service body_bytes_read=%u", (unsigned)payload.body_bytes_read);
				if (valid) {
						payload.result = EpaperNextResult::Show;
						return payload;
				}
				if (attempt + 1 == max_cycles) return payload;
		}
		return empty_payload(EpaperNextResult::FailedFetch);
}

void epaper_next_payload_release(EpaperNextPayload* payload) {
		if (!payload) return;
		if (payload->prepared_data) heap_caps_free(payload->prepared_data);
		if (payload->data) heap_caps_free(payload->data);
		payload->prepared_data = nullptr;
		payload->prepared_len = 0;
		payload->data = nullptr;
		payload->len = 0;
}

#endif
