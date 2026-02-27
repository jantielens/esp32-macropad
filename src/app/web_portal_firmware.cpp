#include "web_portal_firmware.h"

#include "web_portal_auth.h"
#include "web_portal_state.h"

#include "device_telemetry.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include <esp_heap_caps.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "rtos_task_utils.h"

// ===== GitHub Pages firmware update (app-only) =====
static TaskHandle_t firmware_update_task_handle = nullptr;
static RtosTaskPsramAlloc fw_update_task_alloc = {};
static volatile bool firmware_update_in_progress = false;
static volatile size_t firmware_update_progress = 0;
static volatile size_t firmware_update_total = 0;
static volatile uint32_t firmware_update_last_progress_ms = 0;

static char firmware_update_state[16] = "idle"; // idle|downloading|writing|rebooting|error
static char firmware_update_error[192] = "";
static char firmware_update_target_version[24] = "";
static char firmware_update_download_url[512] = "";
static size_t firmware_update_expected_size = 0;

static portMUX_TYPE g_fw_progress_mux = portMUX_INITIALIZER_UNLOCKED;

static inline void firmware_update_set_progress(size_t progress, size_t total) {
		portENTER_CRITICAL(&g_fw_progress_mux);
		firmware_update_progress = progress;
		firmware_update_total = total;
		firmware_update_last_progress_ms = millis();
		portEXIT_CRITICAL(&g_fw_progress_mux);
}

static inline void firmware_update_get_progress(size_t &progress, size_t &total, uint32_t &last_ms) {
		portENTER_CRITICAL(&g_fw_progress_mux);
		progress = firmware_update_progress;
		total = firmware_update_total;
		last_ms = firmware_update_last_progress_ms;
		portEXIT_CRITICAL(&g_fw_progress_mux);
}

// POST /api/firmware/update body accumulator (chunk-safe)
static portMUX_TYPE g_fw_post_mux = portMUX_INITIALIZER_UNLOCKED;
static struct {
		bool in_progress;
		uint32_t started_ms;
		size_t total;
		size_t received;
		uint8_t* buf;
} g_fw_post = {false, 0, 0, 0, nullptr};

static constexpr size_t WEB_PORTAL_FIRMWARE_MAX_JSON_BYTES = 1024;
static constexpr uint32_t WEB_PORTAL_FIRMWARE_BODY_TIMEOUT_MS = 8000;

static void firmware_post_reset() {
		if (g_fw_post.buf) {
				heap_caps_free(g_fw_post.buf);
				g_fw_post.buf = nullptr;
		}
		g_fw_post.in_progress = false;
		g_fw_post.total = 0;
		g_fw_post.received = 0;
		g_fw_post.started_ms = 0;
}

bool web_portal_firmware_update_in_progress() {
		return firmware_update_in_progress;
}

static bool starts_with(const char* s, const char* prefix) {
		if (!s || !prefix) return false;
		const size_t prefix_len = strlen(prefix);
		return strncmp(s, prefix, prefix_len) == 0;
}


static void firmware_update_task(void *pv) {
		(void)pv;

		firmware_update_set_progress(0, firmware_update_total);
		strlcpy(firmware_update_state, "downloading", sizeof(firmware_update_state));
		firmware_update_error[0] = '\0';

		const char *url = firmware_update_download_url;

		web_portal_set_ota_in_progress(true);

		const bool is_https = starts_with(url, "https://");

		HTTPClient http;
		http.setTimeout(30000);

		WiFiClientSecure tls_client;
		WiFiClient plain_client;

		if (is_https) {
				tls_client.setInsecure();
				tls_client.setTimeout(30000);
		} else {
				plain_client.setTimeout(30000);
		}

		int http_code = 0;
		constexpr int kMaxAttempts = 3;
		for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
				http.end();
				http.setReuse(false);
				http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

				const bool began = is_https ? http.begin(tls_client, url) : http.begin(plain_client, url);
				if (!began) {
						LOGE("OTA", "Download start failed attempt %d/%d", attempt, kMaxAttempts);
						LOGE("OTA", "WiFi status=%d RSSI=%d", (int)WiFi.status(), (int)WiFi.RSSI());
						if (attempt < kMaxAttempts) {
								delay(250 * attempt);
						}
						continue;
				}

				http_code = http.GET();
				if (http_code == 200) {
						break;
				}

				const String error_str = http.errorToString(http_code);
				LOGE("OTA", "Download HTTP %d (%s) attempt %d/%d", http_code, error_str.c_str(), attempt, kMaxAttempts);
				LOGE("OTA", "WiFi status=%d RSSI=%d", (int)WiFi.status(), (int)WiFi.RSSI());

				if (attempt < kMaxAttempts) {
						delay(250 * attempt);
				}
		}

		if (http_code != 200) {
				strlcpy(firmware_update_state, "error", sizeof(firmware_update_state));
				snprintf(firmware_update_error, sizeof(firmware_update_error), "Download HTTP %d", http_code);
				http.end();
				firmware_update_in_progress = false;
				web_portal_set_ota_in_progress(false);
				vTaskDelete(nullptr);
				return;
		}

		int http_len = http.getSize();
		size_t total = firmware_update_expected_size;
		if (total == 0 && http_len > 0) {
				total = (size_t)http_len;
		}
		if (http_len <= 0 && total > 0) {
				http_len = (int)total;
		}
		firmware_update_total = total;
		firmware_update_set_progress(firmware_update_progress, firmware_update_total);

		LOGI("OTA", "Download started total=%u", (unsigned)total);

		const size_t freeSpace = device_telemetry_free_sketch_space();
		if (total > 0 && total > freeSpace) {
				strlcpy(firmware_update_state, "error", sizeof(firmware_update_state));
				snprintf(firmware_update_error, sizeof(firmware_update_error), "Firmware too large (%u > %u)", (unsigned)total, (unsigned)freeSpace);
				LOGE("OTA", "Firmware too large total=%u free=%u", (unsigned)total, (unsigned)freeSpace);
				http.end();
				firmware_update_in_progress = false;
				web_portal_set_ota_in_progress(false);
				vTaskDelete(nullptr);
				return;
		}

		if (!Update.begin((total > 0) ? total : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
				strlcpy(firmware_update_state, "error", sizeof(firmware_update_state));
				strlcpy(firmware_update_error, "OTA begin failed", sizeof(firmware_update_error));
				LOGE("OTA", "OTA begin failed");
				http.end();
				firmware_update_in_progress = false;
				web_portal_set_ota_in_progress(false);
				vTaskDelete(nullptr);
				return;
		}

		strlcpy(firmware_update_state, "writing", sizeof(firmware_update_state));

		WiFiClient *stream = http.getStreamPtr();
		uint8_t buf[2048];

		while (http.connected() && (http_len > 0 || http_len == -1)) {
				const size_t available = stream->available();
				if (!available) {
						delay(1);
						continue;
				}

				const size_t to_read = (available > sizeof(buf)) ? sizeof(buf) : available;
				const int read_bytes = stream->readBytes(buf, to_read);
				if (read_bytes <= 0) {
						break;
				}

				const size_t written = Update.write(buf, (size_t)read_bytes);
				if (written != (size_t)read_bytes) {
						strlcpy(firmware_update_state, "error", sizeof(firmware_update_state));
						strlcpy(firmware_update_error, "Flash write failed", sizeof(firmware_update_error));
						LOGE("OTA", "Flash write failed");
						Update.abort();
						http.end();
						firmware_update_in_progress = false;
						web_portal_set_ota_in_progress(false);
						vTaskDelete(nullptr);
						return;
				}

				const size_t new_progress = firmware_update_progress + written;
				firmware_update_set_progress(new_progress, firmware_update_total);
				if (http_len > 0) {
						http_len -= (int)read_bytes;
				}

				// Yield to keep the AsyncTCP task responsive for status polling.
				delay(1);
		}

		http.end();

		if (!Update.end(true)) {
				strlcpy(firmware_update_state, "error", sizeof(firmware_update_state));
				strlcpy(firmware_update_error, "OTA finalize failed", sizeof(firmware_update_error));
				LOGE("OTA", "OTA finalize failed");
				firmware_update_in_progress = false;
				web_portal_set_ota_in_progress(false);
				vTaskDelete(nullptr);
				return;
		}

		strlcpy(firmware_update_state, "rebooting", sizeof(firmware_update_state));

		LOGI("OTA", "Update complete, rebooting");

		// Give the HTTP response/polling a moment to observe completion.
		delay(300);
		ESP.restart();
		vTaskDelete(nullptr);
}

// POST /api/firmware/update - Start background download+OTA from a provided URL.
void handlePostFirmwareUpdate(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
		if (!portal_auth_gate(request)) return;

		// Accumulate the full body (chunk-safe) then parse once.
		if (index == 0) {
				const uint32_t now = millis();

				// Cleanup stale uploads.
				portENTER_CRITICAL(&g_fw_post_mux);
				const bool stale = g_fw_post.in_progress && g_fw_post.started_ms && (now - g_fw_post.started_ms > WEB_PORTAL_FIRMWARE_BODY_TIMEOUT_MS);
				portEXIT_CRITICAL(&g_fw_post_mux);
				if (stale) {
						LOGW("OTA", "Firmware update request timed out; resetting state");
						portENTER_CRITICAL(&g_fw_post_mux);
						firmware_post_reset();
						portEXIT_CRITICAL(&g_fw_post_mux);
				}

				portENTER_CRITICAL(&g_fw_post_mux);
				if (g_fw_post.in_progress) {
						portEXIT_CRITICAL(&g_fw_post_mux);
						request->send(409, "application/json", "{\"success\":false,\"message\":\"Update request already in progress\"}");
						return;
				}
				g_fw_post.in_progress = true;
				g_fw_post.started_ms = now;
				g_fw_post.total = total;
				g_fw_post.received = 0;
				g_fw_post.buf = nullptr;
				portEXIT_CRITICAL(&g_fw_post_mux);

				if (total == 0 || total > WEB_PORTAL_FIRMWARE_MAX_JSON_BYTES) {
						portENTER_CRITICAL(&g_fw_post_mux);
						firmware_post_reset();
						portEXIT_CRITICAL(&g_fw_post_mux);
						request->send(413, "application/json", "{\"success\":false,\"message\":\"JSON body too large\"}");
						return;
				}

				uint8_t* buf = (uint8_t*)heap_caps_malloc(total + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
				if (!buf) {
						portENTER_CRITICAL(&g_fw_post_mux);
						firmware_post_reset();
						portEXIT_CRITICAL(&g_fw_post_mux);
						request->send(503, "application/json", "{\"success\":false,\"message\":\"Out of memory\"}");
						return;
				}

				portENTER_CRITICAL(&g_fw_post_mux);
				g_fw_post.buf = buf;
				portEXIT_CRITICAL(&g_fw_post_mux);
		}

		// Copy this chunk.
		portENTER_CRITICAL(&g_fw_post_mux);
		const bool ok = g_fw_post.in_progress && g_fw_post.buf && g_fw_post.total == total && (index + len) <= total;
		uint8_t* dst = g_fw_post.buf;
		portEXIT_CRITICAL(&g_fw_post_mux);

		if (!ok) {
				request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid upload state\"}");
				portENTER_CRITICAL(&g_fw_post_mux);
				firmware_post_reset();
				portEXIT_CRITICAL(&g_fw_post_mux);
				return;
		}

		memcpy(dst + index, data, len);

		portENTER_CRITICAL(&g_fw_post_mux);
		const size_t new_received = index + len;
		if (new_received > g_fw_post.received) {
				g_fw_post.received = new_received;
		}
		const bool done = (g_fw_post.received >= g_fw_post.total);
		portEXIT_CRITICAL(&g_fw_post_mux);

		if (!done) {
				return;
		}

		// Finalize buffer and parse.
		uint8_t* body = nullptr;
		size_t body_len = 0;
		portENTER_CRITICAL(&g_fw_post_mux);
		body = g_fw_post.buf;
		body_len = g_fw_post.total;
		if (body) body[body_len] = 0;
		portEXIT_CRITICAL(&g_fw_post_mux);

		BasicJsonDocument<PsramJsonAllocator> doc(768);
		DeserializationError error = deserializeJson(doc, body, body_len);

		if (error) {
				LOGE("OTA", "JSON parse error: %s", error.c_str());
				request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
				portENTER_CRITICAL(&g_fw_post_mux);
				firmware_post_reset();
				portEXIT_CRITICAL(&g_fw_post_mux);
				return;
		}

		const char *url = doc["url"] | "";
		const char *version = doc["version"] | "";
		const size_t size = (size_t)(doc["size"] | 0);

		if (!url || strlen(url) == 0) {
				request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing firmware URL\"}");
				portENTER_CRITICAL(&g_fw_post_mux);
				firmware_post_reset();
				portEXIT_CRITICAL(&g_fw_post_mux);
				return;
		}

		if (!starts_with(url, "http://") && !starts_with(url, "https://")) {
				request->send(400, "application/json", "{\"success\":false,\"message\":\"URL must be http(s)\"}");
				portENTER_CRITICAL(&g_fw_post_mux);
				firmware_post_reset();
				portEXIT_CRITICAL(&g_fw_post_mux);
				return;
		}

		if (web_portal_ota_in_progress() || firmware_update_in_progress) {
				request->send(409, "application/json", "{\"success\":false,\"message\":\"Update already in progress\"}");
				portENTER_CRITICAL(&g_fw_post_mux);
				firmware_post_reset();
				portEXIT_CRITICAL(&g_fw_post_mux);
				return;
		}

		if (WiFi.status() != WL_CONNECTED) {
				request->send(409, "application/json", "{\"success\":false,\"message\":\"WiFi not connected\"}");
				portENTER_CRITICAL(&g_fw_post_mux);
				firmware_post_reset();
				portEXIT_CRITICAL(&g_fw_post_mux);
				return;
		}

		// Seed global state for status polling.
		firmware_update_in_progress = true;
		firmware_update_progress = 0;
		firmware_update_total = size;
		firmware_update_last_progress_ms = millis();
		firmware_update_set_progress(0, size);
		firmware_update_expected_size = size;
		strlcpy(firmware_update_target_version, version, sizeof(firmware_update_target_version));
		strlcpy(firmware_update_download_url, url, sizeof(firmware_update_download_url));
		firmware_update_error[0] = '\0';
		strlcpy(firmware_update_state, "downloading", sizeof(firmware_update_state));

		LOGI("OTA", "Update requested url=%s size=%u", url, (unsigned)size);

		// Spawn background task to avoid blocking AsyncTCP.
		// Try PSRAM stack first (saves 12 KB internal RAM), fall back to internal.
		bool task_ok = rtos_create_task_psram_stack(
				firmware_update_task,
				"fw_update",
				12288,
				nullptr,
				1,
				&firmware_update_task_handle,
				&fw_update_task_alloc
		);
		if (!task_ok) {
				task_ok = (xTaskCreate(firmware_update_task, "fw_update", 12288, nullptr, 1, &firmware_update_task_handle) == pdPASS);
		}

		if (!task_ok) {
				firmware_update_in_progress = false;
				strlcpy(firmware_update_state, "error", sizeof(firmware_update_state));
				strlcpy(firmware_update_error, "Failed to start update task", sizeof(firmware_update_error));
				LOGE("OTA", "Failed to start update task");
				request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to start update\"}");
				portENTER_CRITICAL(&g_fw_post_mux);
				firmware_post_reset();
				portEXIT_CRITICAL(&g_fw_post_mux);
				return;
		}

		std::shared_ptr<BasicJsonDocument<PsramJsonAllocator>> resp = make_psram_json_doc(384);
		if (resp && resp->capacity() > 0) {
				(*resp)["success"] = true;
				(*resp)["update_started"] = true;
				(*resp)["version"] = firmware_update_target_version;
		}

		web_portal_send_json_chunked(request, resp);

		portENTER_CRITICAL(&g_fw_post_mux);
		firmware_post_reset();
		portEXIT_CRITICAL(&g_fw_post_mux);
}

// GET /api/firmware/update/status - Progress snapshot for online update.
void handleGetFirmwareUpdateStatus(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		std::shared_ptr<BasicJsonDocument<PsramJsonAllocator>> doc = make_psram_json_doc(640);
		if (doc && doc->capacity() > 0) {
				size_t progress = 0;
				size_t total = 0;
				uint32_t last_ms = 0;
				firmware_update_get_progress(progress, total, last_ms);
				(*doc)["in_progress"] = firmware_update_in_progress;
				(*doc)["state"] = firmware_update_state;
				(*doc)["progress"] = (uint32_t)progress;
				(*doc)["total"] = (uint32_t)total;
				(*doc)["version"] = firmware_update_target_version;
				(*doc)["error"] = firmware_update_error;
				(*doc)["last_progress_ms"] = last_ms;
		}

		web_portal_send_json_chunked(request, doc);
}
