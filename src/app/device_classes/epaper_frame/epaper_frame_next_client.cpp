#include "board_config.h"

#if IS_EPAPER_FRAME && defined(BOARD_RETERMINAL_E1003_FRAME)

#include "device_classes/epaper_frame/epaper_frame_next_client.h"

#include "device_classes/epaper_frame/epaper_frame_driver.h"
#include "device_classes/epaper_frame/epaper_frame_http.h"
#include "device_classes/epaper_frame/epaper_frame_sd_cache.h"
#include "device_classes/epaper_frame/epaper_frame_timing.h"
#include "device_classes/epaper_frame/epaper_frame_transport_crc32.h"
#include "log_manager.h"
#include "psram_json_allocator.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace {

constexpr uint32_t kServiceHttpTimeoutMs = 7000;
constexpr size_t kMaxBatchManifestBytes = 32 * 1024;
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

String endpoint_url(const char* service_base, const char* endpoint) {
		String url = service_base ? service_base : "";
		while (url.endsWith("/")) url.remove(url.length() - 1);
		url += endpoint;
		return url;
}

String batch_url(const char* service_base, uint8_t count) {
		String url = endpoint_url(service_base, "/api/v1/next-batch?count=");
		url += String(count);
		return url;
}

String resolve_content_ref(const char* service_base, const char* content_ref) {
		String ref = content_ref ? content_ref : "";
		if (ref.startsWith("http://") || ref.startsWith("https://")) return ref;
		String base = service_base ? service_base : "";
		if (!ref.startsWith("/")) {
				while (base.endsWith("/")) base.remove(base.length() - 1);
				return base + "/" + ref;
		}
		const int scheme_end = base.indexOf("://");
		if (scheme_end < 0) return String();
		const int path_start = base.indexOf('/', scheme_end + 3);
		if (path_start >= 0) base.remove(path_start);
		return base + ref;
}

bool begin_request(HTTPClient& http, WiFiClient& plain,
		WiFiClientSecure& secure, const String& url) {
		http.useHTTP10(true);
		if (url.startsWith("https://")) {
				secure.setInsecure();
				return http.begin(secure, url);
		}
		return url.startsWith("http://") && http.begin(plain, url);
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

void add_request_auth_and_fingerprint(HTTPClient& http,
		const char* bearer_token, const EpaperCurrentFingerprint* current) {
		http.addHeader("Authorization", String("Bearer ") + bearer_token);
		if (!current || !current->valid) return;
		char crc_text[9];
		snprintf(crc_text, sizeof(crc_text), "%08lx",
				(unsigned long)current->content_crc32);
		http.addHeader("Photoframe-Current-Image-Key", current->image_key);
		http.addHeader("Photoframe-Current-Content-CRC32", crc_text);
}

bool parse_show_metadata(HTTPClient& http, ShowMetadata* metadata) {
		const String image_key = http.header(kImageKeyHeader);
		const String crc_text = http.header(kContentCrcHeader);
		if (!metadata ||
				!epaper_frame_next_valid_image_key(image_key.c_str()) ||
				!epaper_frame_next_parse_crc32(
						crc_text.c_str(), &metadata->content_crc32)) {
				return false;
		}
		strlcpy(metadata->image_key, image_key.c_str(),
				sizeof(metadata->image_key));
		return true;
}

bool valid_content_headers(HTTPClient& http, char* media_type,
		size_t media_type_size) {
		const String encoding = http.header("Content-Encoding");
		if (encoding.length() > 0 &&
				!encoding.equalsIgnoreCase("identity")) return false;
		const String type = http.header("Content-Type");
		const bool supported = type == "image/jpeg" ||
				type == "application/vnd.photoframe.g16p" ||
				type == "application/vnd.photoframe.g16z";
		if (!supported) return false;
		strlcpy(media_type, type.c_str(), media_type_size);
		return true;
}

bool try_cache(uint32_t content_crc32, const char* media_type,
		EpaperNextPayload* payload, uint32_t expected_length = 0) {
		const uint32_t started = millis();
		uint8_t* data = nullptr;
		size_t len = 0;
		if (!epaper_frame_sd_cache_read(
				content_crc32, &data, &len)) return false;
		const bool valid = (expected_length == 0 ||
				len == expected_length) &&
				epaper_frame_transport_crc32(data, len) == content_crc32 &&
				epaper_frame_driver_prepare_service_blob(
						data, len, media_type,
						&payload->prepared_data, &payload->prepared_len);
		if (!valid) {
				heap_caps_free(data);
				epaper_frame_sd_cache_remove(content_crc32);
				LOGW("Epaper", "Service cache entry failed revalidation; evicted");
				return false;
		}
		payload->data = data;
		payload->len = len;
		payload->from_cache = true;
		payload->body_bytes_read = 0;
		epaper_frame_timing_set_fetch(millis() - started, true);
		return true;
}

bool read_and_validate_body(HTTPClient& http,
		const ShowMetadata& metadata, const char* media_type,
		EpaperNextPayload* payload, uint32_t expected_length = 0,
		uint32_t idle_timeout_ms = kServiceHttpTimeoutMs) {
		const uint32_t started = millis();
		uint8_t* data = nullptr;
		size_t len = 0;
		size_t body_bytes_read = 0;
		if (!epaper_frame_http_read_body(
				http, &data, &len, &body_bytes_read,
				expected_length > 0, idle_timeout_ms)) {
				return false;
		}
		payload->body_bytes_read = body_bytes_read;
		if ((expected_length > 0 && len != expected_length) ||
				epaper_frame_transport_crc32(data, len) !=
						metadata.content_crc32 ||
				!epaper_frame_driver_prepare_service_blob(
						data, len, media_type,
						&payload->prepared_data, &payload->prepared_len)) {
				heap_caps_free(data);
				return false;
		}
		payload->data = data;
		payload->len = len;
		epaper_frame_timing_set_fetch(millis() - started, false);
		return true;
}

bool parse_batch_entry(JsonObjectConst source, EpaperBatchEntry* entry) {
		if (!entry) return false;
		const char* image_key = source["image_key"] | "";
		const char* crc_text = source["content_crc32"] | "";
		const char* media_type = source["media_type"] | "";
		const char* content_ref = source["content_url"] | "";
		if (!*content_ref) content_ref = source["content_ref"] | "";
		uint32_t content_crc32 = 0;
		const bool media_supported =
				strcmp(media_type, "image/jpeg") == 0 ||
				strcmp(media_type, "application/vnd.photoframe.g16p") == 0 ||
				strcmp(media_type, "application/vnd.photoframe.g16z") == 0;
		if (!epaper_frame_next_valid_image_key(image_key) ||
				!epaper_frame_next_parse_crc32(
						crc_text, &content_crc32) ||
				!media_supported || !*content_ref ||
				strlen(content_ref) >= sizeof(entry->content_ref) ||
				!source["content_length"].is<uint32_t>()) {
				return false;
		}
		const uint32_t content_length =
				source["content_length"].as<uint32_t>();
		if (content_length == 0) return false;
		strlcpy(entry->image_key, image_key, sizeof(entry->image_key));
		strlcpy(entry->media_type, media_type, sizeof(entry->media_type));
		strlcpy(entry->content_ref, content_ref, sizeof(entry->content_ref));
		entry->content_crc32 = content_crc32;
		entry->content_length = content_length;
		return true;
}

EpaperNextPayload follow_redirect(const String& location,
		const ShowMetadata& metadata, bool cache_enabled,
		uint32_t timeout_ms) {
		EpaperNextPayload payload =
				empty_payload(EpaperNextResult::FailedFetch);
		strlcpy(payload.image_key, metadata.image_key,
				sizeof(payload.image_key));
		payload.content_crc32 = metadata.content_crc32;

		WiFiClient plain;
		WiFiClientSecure secure;
		HTTPClient http;
		if (!begin_request(http, plain, secure, location)) return payload;
		http.setTimeout(timeout_ms);
		collect_service_headers(http);
		const int status = http.GET();
		if (status != HTTP_CODE_OK ||
				!valid_content_headers(http, payload.media_type,
						sizeof(payload.media_type))) {
				http.end();
				return payload;
		}
		if (cache_enabled && try_cache(metadata.content_crc32,
				payload.media_type, &payload)) {
				http.end();
				payload.result = EpaperNextResult::Show;
				LOGI("Epaper", "Service redirect cache hit; body_bytes_read=0");
				return payload;
		}
		const bool ok = read_and_validate_body(
				http, metadata, payload.media_type, &payload,
				0, timeout_ms);
		http.end();
		delay(100);
		payload.result = ok ? EpaperNextResult::Show
				: EpaperNextResult::FailedContent;
		return payload;
}

} // namespace

EpaperNextPayload epaper_frame_next_client_fetch(
		const char* service_base, const char* bearer_token,
		const EpaperCurrentFingerprint& current, bool cache_enabled,
		uint8_t max_cycles, uint32_t timeout_ms) {
		if (!service_base || !*service_base ||
				!bearer_token || !*bearer_token || max_cycles == 0) {
				return empty_payload(EpaperNextResult::FailedFetch);
		}
		if (max_cycles > 2) max_cycles = 2;
		const uint32_t request_timeout_ms =
				timeout_ms > 0 ? timeout_ms : kServiceHttpTimeoutMs;
		if (timeout_ms > 0) max_cycles = 1;
		const String url = endpoint_url(service_base, "/api/v1/next");

		for (uint8_t attempt = 0; attempt < max_cycles; ++attempt) {
				WiFiClient plain;
				WiFiClientSecure secure;
				HTTPClient http;
				if (!begin_request(http, plain, secure, url)) {
						return empty_payload(EpaperNextResult::FailedFetch);
				}
				http.setTimeout(request_timeout_ms);
				collect_service_headers(http);
				add_request_auth_and_fingerprint(
						http, bearer_token, &current);

				const int status = http.GET();
				const EpaperNextAction action =
						epaper_frame_next_action_for_status(status);
				if (action == EpaperNextAction::Keep) {
						http.end();
						return empty_payload(EpaperNextResult::Keep);
				}
				if (action == EpaperNextAction::AuthFailed) {
						const bool bearer_challenge =
								http.header("WWW-Authenticate") == "Bearer";
						http.end();
						LOGW("Epaper", "Service authentication failed%s",
								bearer_challenge ? "" : " (missing ******");
						return empty_payload(EpaperNextResult::AuthFailed);
				}
				if (action == EpaperNextAction::UnsupportedMajor) {
						http.end();
						return empty_payload(
								EpaperNextResult::UnsupportedMajor);
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
						return empty_payload(EpaperNextResult::FailedContent);
				}
				if (action == EpaperNextAction::FollowRedirect) {
						const String location = http.header("Location");
						http.end();
						if (location.length() == 0) {
								return empty_payload(
										EpaperNextResult::FailedFetch);
						}
						return follow_redirect(location, metadata,
								cache_enabled, request_timeout_ms);
				}

				EpaperNextPayload payload =
						empty_payload(EpaperNextResult::FailedContent);
				strlcpy(payload.image_key, metadata.image_key,
						sizeof(payload.image_key));
				payload.content_crc32 = metadata.content_crc32;
				if (!valid_content_headers(http, payload.media_type,
						sizeof(payload.media_type))) {
						http.end();
						return payload;
				}
				if (cache_enabled && try_cache(metadata.content_crc32,
						payload.media_type, &payload)) {
						http.end();
						payload.result = EpaperNextResult::Show;
						LOGI("Epaper", "Service cache hit; body_bytes_read=0");
						return payload;
				}
				const bool valid = read_and_validate_body(
						http, metadata, payload.media_type, &payload,
						0, request_timeout_ms);
				http.end();
				delay(100);
				LOGI("Epaper", "Service body_bytes_read=%u",
						(unsigned)payload.body_bytes_read);
				if (valid) payload.result = EpaperNextResult::Show;
				return payload;
		}
		return empty_payload(EpaperNextResult::FailedFetch);
}

EpaperNextResult epaper_frame_next_client_fetch_batch_manifest(
		const char* service_base, const char* bearer_token,
		const EpaperCurrentFingerprint& current, uint8_t requested_count,
		EpaperBatchManifest** manifest, uint32_t timeout_ms) {
		if (manifest) *manifest = nullptr;
		if (!manifest || !service_base || !*service_base ||
				!bearer_token || !*bearer_token ||
				requested_count < 1 ||
				requested_count > EPAPER_FRAME_BATCH_MAX_COUNT) {
				return EpaperNextResult::FailedFetch;
		}
		const uint32_t request_timeout_ms =
				timeout_ms > 0 ? timeout_ms : kServiceHttpTimeoutMs;
		WiFiClient plain;
		WiFiClientSecure secure;
		HTTPClient http;
		if (!begin_request(http, plain, secure,
				batch_url(service_base, requested_count))) {
				return EpaperNextResult::FailedFetch;
		}
		http.setTimeout(request_timeout_ms);
		collect_service_headers(http);
		add_request_auth_and_fingerprint(http, bearer_token, &current);
		const int status = http.GET();
		if (status == HTTP_CODE_NO_CONTENT) {
				http.end();
				return EpaperNextResult::Keep;
		}
		if (status == HTTP_CODE_UNAUTHORIZED) {
				http.end();
				return EpaperNextResult::AuthFailed;
		}
		if (status != HTTP_CODE_OK) {
				http.end();
				return EpaperNextResult::FailedFetch;
		}
		const int content_length = http.getSize();
		if (content_length <= 0 ||
				static_cast<size_t>(content_length) >
						kMaxBatchManifestBytes) {
				http.end();
				return EpaperNextResult::FailedContent;
		}
		uint8_t* body = nullptr;
		size_t body_length = 0;
		size_t body_bytes_read = 0;
		if (!epaper_frame_http_read_body(
				http, &body, &body_length, &body_bytes_read,
				true, request_timeout_ms)) {
				http.end();
				return EpaperNextResult::FailedFetch;
		}
		http.end();
		delay(100);

		BasicJsonDocument<PsramJsonAllocator> document(kMaxBatchManifestBytes);
		const DeserializationError error =
				deserializeJson(document, body, body_length);
		heap_caps_free(body);
		if (error) return EpaperNextResult::FailedContent;
		JsonArrayConst images = document["images"].as<JsonArrayConst>();
		if (images.isNull() || images.size() == 0 ||
				images.size() > requested_count ||
				images.size() > EPAPER_FRAME_BATCH_MAX_COUNT) {
				return EpaperNextResult::FailedContent;
		}

		EpaperBatchManifest* parsed =
				static_cast<EpaperBatchManifest*>(heap_caps_calloc(
						1, sizeof(EpaperBatchManifest),
						MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
		if (!parsed) return EpaperNextResult::FailedFetch;
		for (JsonObjectConst image : images) {
				if (!parse_batch_entry(
						image, &parsed->entries[parsed->count])) {
						LOGW("Epaper", "Batch manifest stopped at invalid entry %u",
								(unsigned)parsed->count);
						break;
				}
				bool duplicate = false;
				for (uint8_t index = 0; index < parsed->count; ++index) {
						if (parsed->entries[index].content_crc32 ==
										parsed->entries[parsed->count].content_crc32 &&
								strcmp(parsed->entries[index].image_key,
										parsed->entries[parsed->count].image_key) == 0) {
								duplicate = true;
								break;
						}
				}
				if (duplicate) {
						LOGW("Epaper", "Batch manifest stopped at duplicate entry %u",
								(unsigned)parsed->count);
						break;
				}
				++parsed->count;
		}
		if (parsed->count == 0) {
				heap_caps_free(parsed);
				return EpaperNextResult::FailedContent;
		}
		*manifest = parsed;
		return EpaperNextResult::Show;
}

EpaperNextPayload epaper_frame_next_client_fetch_batch_entry(
		const char* service_base, const char* bearer_token,
		const EpaperBatchEntry& entry, bool cache_enabled,
		uint32_t timeout_ms) {
		EpaperNextPayload payload =
				empty_payload(EpaperNextResult::FailedFetch);
		strlcpy(payload.image_key, entry.image_key,
				sizeof(payload.image_key));
		strlcpy(payload.media_type, entry.media_type,
				sizeof(payload.media_type));
		payload.content_crc32 = entry.content_crc32;
		if (cache_enabled && try_cache(
				entry.content_crc32, entry.media_type,
				&payload, entry.content_length)) {
				payload.result = EpaperNextResult::Show;
				LOGI("Epaper", "Batch cache hit; body_bytes_read=0");
				return payload;
		}

		const String url =
				resolve_content_ref(service_base, entry.content_ref);
		if (url.length() == 0 || !bearer_token || !*bearer_token) {
				return payload;
		}
		const uint32_t request_timeout_ms =
				timeout_ms > 0 ? timeout_ms : kServiceHttpTimeoutMs;
		WiFiClient plain;
		WiFiClientSecure secure;
		HTTPClient http;
		if (!begin_request(http, plain, secure, url)) return payload;
		http.setTimeout(request_timeout_ms);
		collect_service_headers(http);
		add_request_auth_and_fingerprint(http, bearer_token, nullptr);
		const int status = http.GET();
		if (status != HTTP_CODE_OK ||
				http.getSize() != static_cast<int>(entry.content_length)) {
				http.end();
				return payload;
		}
		ShowMetadata response_metadata = {};
		if (!parse_show_metadata(http, &response_metadata) ||
				strcmp(response_metadata.image_key, entry.image_key) != 0 ||
				response_metadata.content_crc32 != entry.content_crc32) {
				http.end();
				payload.result = EpaperNextResult::FailedContent;
				return payload;
		}
		char response_media_type[sizeof(payload.media_type)] = {};
		if (!valid_content_headers(http, response_media_type,
				sizeof(response_media_type)) ||
				strcmp(response_media_type, entry.media_type) != 0) {
				http.end();
				payload.result = EpaperNextResult::FailedContent;
				return payload;
		}
		const bool valid = read_and_validate_body(
				http, response_metadata, entry.media_type, &payload,
				entry.content_length, request_timeout_ms);
		http.end();
		delay(100);
		payload.result = valid ? EpaperNextResult::Show
				: EpaperNextResult::FailedContent;
		return payload;
}

void epaper_frame_batch_manifest_release(EpaperBatchManifest* manifest) {
		if (manifest) heap_caps_free(manifest);
}

void epaper_frame_next_payload_release(EpaperNextPayload* payload) {
		if (!payload) return;
		if (payload->prepared_data) heap_caps_free(payload->prepared_data);
		if (payload->data) heap_caps_free(payload->data);
		payload->prepared_data = nullptr;
		payload->prepared_len = 0;
		payload->data = nullptr;
		payload->len = 0;
}

#endif
