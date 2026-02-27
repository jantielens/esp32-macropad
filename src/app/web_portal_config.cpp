#include "web_portal_config.h"

#include "web_portal_auth.h"
#include "web_portal_state.h"
#include "portal_idle.h"

#include "board_config.h"
#include "config_manager.h"
#include "device_telemetry.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "web_portal_json.h"

#if HAS_DISPLAY
#include "display_manager.h"
#include "screen_saver_manager.h"
#endif

#include <ArduinoJson.h>
#include <WiFi.h>

#include <esp_heap_caps.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// /api/config body accumulator (chunk-safe)
static portMUX_TYPE g_config_post_mux = portMUX_INITIALIZER_UNLOCKED;
static struct {
		bool in_progress;
		uint32_t started_ms;
		size_t total;
		size_t received;
		uint8_t* buf;
} g_config_post = {false, 0, 0, 0, nullptr};

static void config_post_reset() {
		if (g_config_post.buf) {
				heap_caps_free(g_config_post.buf);
				g_config_post.buf = nullptr;
		}
		g_config_post.in_progress = false;
		g_config_post.total = 0;
		g_config_post.received = 0;
		g_config_post.started_ms = 0;
		portal_idle_set_config_upload_in_progress(false);
}

void web_portal_config_loop() {
		// Cleanup stuck /api/config uploads.
		const uint32_t now = millis();
		portENTER_CRITICAL(&g_config_post_mux);
		const bool stale = g_config_post.in_progress && g_config_post.started_ms && (now - g_config_post.started_ms > WEB_PORTAL_CONFIG_BODY_TIMEOUT_MS);
		if (stale) {
				config_post_reset();
		}
		portEXIT_CRITICAL(&g_config_post_mux);

		if (stale) {
				LOGW("Portal", "Config upload timed out (loop cleanup)");
		}
}

void handleGetConfig(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		DeviceConfig *current_config = web_portal_get_current_config();
		if (!current_config) {
				request->send(500, "application/json", "{\"error\":\"Config not initialized\"}");
				return;
		}

		// Create JSON response (don't include passwords)
		std::shared_ptr<BasicJsonDocument<PsramJsonAllocator>> doc = make_psram_json_doc(2304);
		if (doc && doc->capacity() > 0) {
				(*doc)["wifi_ssid"] = current_config->wifi_ssid;
				(*doc)["wifi_password"] = ""; // Don't send password
				(*doc)["device_name"] = current_config->device_name;

				// Sanitized name for display
				char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
				config_manager_sanitize_device_name(current_config->device_name, sanitized, CONFIG_DEVICE_NAME_MAX_LEN);
				(*doc)["device_name_sanitized"] = sanitized;

				// Fixed IP settings
				(*doc)["fixed_ip"] = current_config->fixed_ip;
				(*doc)["subnet_mask"] = current_config->subnet_mask;
				(*doc)["gateway"] = current_config->gateway;
				(*doc)["dns1"] = current_config->dns1;
				(*doc)["dns2"] = current_config->dns2;

				// Dummy setting
				(*doc)["dummy_setting"] = current_config->dummy_setting;

				// MQTT settings (password not returned)
				(*doc)["mqtt_host"] = current_config->mqtt_host;
				(*doc)["mqtt_port"] = current_config->mqtt_port;
				(*doc)["mqtt_username"] = current_config->mqtt_username;
				(*doc)["mqtt_password"] = "";

				// Power / transport settings
				(*doc)["power_mode"] = current_config->power_mode;
				(*doc)["publish_transport"] = current_config->publish_transport;
				(*doc)["cycle_interval_seconds"] = current_config->cycle_interval_seconds;
				(*doc)["portal_idle_timeout_seconds"] = current_config->portal_idle_timeout_seconds;
				(*doc)["wifi_backoff_max_seconds"] = current_config->wifi_backoff_max_seconds;

				// BLE timing
				(*doc)["ble_adv_burst_ms"] = current_config->ble_adv_burst_ms;
				(*doc)["ble_adv_gap_ms"] = current_config->ble_adv_gap_ms;
				(*doc)["ble_adv_bursts"] = current_config->ble_adv_bursts;
				(*doc)["ble_adv_interval_ms"] = current_config->ble_adv_interval_ms;

				// MQTT scope
				(*doc)["mqtt_publish_scope"] = current_config->mqtt_publish_scope;

				// Web portal Basic Auth (password not returned)
				(*doc)["basic_auth_enabled"] = current_config->basic_auth_enabled;
				(*doc)["basic_auth_username"] = current_config->basic_auth_username;
				(*doc)["basic_auth_password"] = "";
				(*doc)["basic_auth_password_set"] = (strlen(current_config->basic_auth_password) > 0);

				// Display settings
				(*doc)["backlight_brightness"] = current_config->backlight_brightness;

				#if HAS_DISPLAY
				// Screen saver settings
				(*doc)["screen_saver_enabled"] = current_config->screen_saver_enabled;
				(*doc)["screen_saver_timeout_seconds"] = current_config->screen_saver_timeout_seconds;
				(*doc)["screen_saver_fade_out_ms"] = current_config->screen_saver_fade_out_ms;
				(*doc)["screen_saver_fade_in_ms"] = current_config->screen_saver_fade_in_ms;
				(*doc)["screen_saver_wake_on_touch"] = current_config->screen_saver_wake_on_touch;
				#endif

				if (doc->overflowed()) {
						LOGE("Portal", "/api/config JSON overflow");
				}
		}

		web_portal_send_json_chunked(request, doc);
}

void handlePostConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
		if (!portal_auth_gate(request)) return;

		DeviceConfig *current_config = web_portal_get_current_config();
		if (!current_config) {
				request->send(500, "application/json", "{\"success\":false,\"message\":\"Config not initialized\"}");
				return;
		}

		// Accumulate the full body (chunk-safe) then parse once.
		if (index == 0) {
				// If a previous upload got stuck, reset it.
				const uint32_t now = millis();
				portENTER_CRITICAL(&g_config_post_mux);
				const bool stale = g_config_post.in_progress && g_config_post.started_ms && (now - g_config_post.started_ms > WEB_PORTAL_CONFIG_BODY_TIMEOUT_MS);
				portEXIT_CRITICAL(&g_config_post_mux);
				if (stale) {
						LOGW("Portal", "Config upload timed out; resetting state");
						portENTER_CRITICAL(&g_config_post_mux);
						config_post_reset();
						portEXIT_CRITICAL(&g_config_post_mux);
				}

				portENTER_CRITICAL(&g_config_post_mux);
				if (g_config_post.in_progress) {
						portEXIT_CRITICAL(&g_config_post_mux);
						request->send(409, "application/json", "{\"success\":false,\"message\":\"Config update already in progress\"}");
						return;
				}
				g_config_post.in_progress = true;
				g_config_post.started_ms = now;
				g_config_post.total = total;
				g_config_post.received = 0;
				g_config_post.buf = nullptr;
				portal_idle_set_config_upload_in_progress(true);
				portEXIT_CRITICAL(&g_config_post_mux);

				if (total == 0 || total > WEB_PORTAL_CONFIG_MAX_JSON_BYTES) {
						portENTER_CRITICAL(&g_config_post_mux);
						config_post_reset();
						portEXIT_CRITICAL(&g_config_post_mux);
						request->send(413, "application/json", "{\"success\":false,\"message\":\"JSON body too large\"}");
						return;
				}

				uint8_t* buf = nullptr;
				if (psramFound()) {
						buf = (uint8_t*)heap_caps_malloc(total + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
				}
				if (!buf) {
						buf = (uint8_t*)heap_caps_malloc(total + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
				}
				if (!buf) {
						portENTER_CRITICAL(&g_config_post_mux);
						config_post_reset();
						portEXIT_CRITICAL(&g_config_post_mux);
						request->send(503, "application/json", "{\"success\":false,\"message\":\"Out of memory\"}");
						return;
				}

				portENTER_CRITICAL(&g_config_post_mux);
				g_config_post.buf = buf;
				portEXIT_CRITICAL(&g_config_post_mux);
		}

		// Copy this chunk.
		portENTER_CRITICAL(&g_config_post_mux);
		const bool ok = g_config_post.in_progress && g_config_post.buf && g_config_post.total == total && (index + len) <= total;
		uint8_t* dst = g_config_post.buf;
		portEXIT_CRITICAL(&g_config_post_mux);

		if (!ok) {
				request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid upload state\"}");
				portENTER_CRITICAL(&g_config_post_mux);
				config_post_reset();
				portEXIT_CRITICAL(&g_config_post_mux);
				return;
		}

		memcpy(dst + index, data, len);

		portENTER_CRITICAL(&g_config_post_mux);
		const size_t new_received = index + len;
		if (new_received > g_config_post.received) {
				g_config_post.received = new_received;
		}
		const bool done = (g_config_post.received >= g_config_post.total);
		portEXIT_CRITICAL(&g_config_post_mux);

		if (!done) {
				// More chunks to come.
				return;
		}

		// Finalize buffer and parse.
		uint8_t* body = nullptr;
		size_t body_len = 0;
		portENTER_CRITICAL(&g_config_post_mux);
		body = g_config_post.buf;
		body_len = g_config_post.total;
		if (body) body[body_len] = 0;
		portEXIT_CRITICAL(&g_config_post_mux);

		BasicJsonDocument<PsramJsonAllocator> doc(2304);
		DeserializationError error = deserializeJson(doc, body, body_len);

		if (error) {
				LOGE("Portal", "JSON parse error: %s", error.c_str());
				if (error == DeserializationError::NoMemory) {
						request->send(413, "application/json", "{\"success\":false,\"message\":\"JSON body too large\"}");
						portENTER_CRITICAL(&g_config_post_mux);
						config_post_reset();
						portEXIT_CRITICAL(&g_config_post_mux);
						return;
				}
				request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
				portENTER_CRITICAL(&g_config_post_mux);
				config_post_reset();
				portEXIT_CRITICAL(&g_config_post_mux);
				return;
		}

		// Partial update: only update fields that are present in the request
		// This allows different pages to update only their relevant fields

		// Security hardening: never allow changing Basic Auth settings in AP/core mode.
		// Otherwise, an attacker near the device could wait for fallback AP mode and lock out the owner.
		if (web_portal_is_ap_mode_active() && (doc.containsKey("basic_auth_enabled") || doc.containsKey("basic_auth_username") || doc.containsKey("basic_auth_password"))) {
				request->send(403, "application/json", "{\"success\":false,\"message\":\"Basic Auth settings cannot be changed in AP mode\"}");
				portENTER_CRITICAL(&g_config_post_mux);
				config_post_reset();
				portEXIT_CRITICAL(&g_config_post_mux);
				return;
		}

		// WiFi SSID - only update if field exists in JSON
		if (doc.containsKey("wifi_ssid")) {
				strlcpy(current_config->wifi_ssid, doc["wifi_ssid"] | "", CONFIG_SSID_MAX_LEN);
		}

		// WiFi password - only update if provided and not empty
		if (doc.containsKey("wifi_password")) {
				const char* wifi_pass = doc["wifi_password"];
				if (wifi_pass && strlen(wifi_pass) > 0) {
						strlcpy(current_config->wifi_password, wifi_pass, CONFIG_PASSWORD_MAX_LEN);
				}
		}

		// Device name - only update if field exists
		if (doc.containsKey("device_name")) {
				const char* device_name = doc["device_name"];
				if (device_name && strlen(device_name) > 0) {
						strlcpy(current_config->device_name, device_name, CONFIG_DEVICE_NAME_MAX_LEN);
				}
		}

		// Fixed IP settings - only update if fields exist
		if (doc.containsKey("fixed_ip")) {
				strlcpy(current_config->fixed_ip, doc["fixed_ip"] | "", CONFIG_IP_STR_MAX_LEN);
		}
		if (doc.containsKey("subnet_mask")) {
				strlcpy(current_config->subnet_mask, doc["subnet_mask"] | "", CONFIG_IP_STR_MAX_LEN);
		}
		if (doc.containsKey("gateway")) {
				strlcpy(current_config->gateway, doc["gateway"] | "", CONFIG_IP_STR_MAX_LEN);
		}
		if (doc.containsKey("dns1")) {
				strlcpy(current_config->dns1, doc["dns1"] | "", CONFIG_IP_STR_MAX_LEN);
		}
		if (doc.containsKey("dns2")) {
				strlcpy(current_config->dns2, doc["dns2"] | "", CONFIG_IP_STR_MAX_LEN);
		}

		// Dummy setting - only update if field exists
		if (doc.containsKey("dummy_setting")) {
				strlcpy(current_config->dummy_setting, doc["dummy_setting"] | "", CONFIG_DUMMY_MAX_LEN);
		}

		// MQTT host
		if (doc.containsKey("mqtt_host")) {
				strlcpy(current_config->mqtt_host, doc["mqtt_host"] | "", CONFIG_MQTT_HOST_MAX_LEN);
		}

		// MQTT port (optional; 0 means default 1883)
		if (doc.containsKey("mqtt_port")) {
				if (doc["mqtt_port"].is<const char*>()) {
						const char* port_str = doc["mqtt_port"];
						current_config->mqtt_port = (uint16_t)atoi(port_str ? port_str : "0");
				} else {
						current_config->mqtt_port = (uint16_t)(doc["mqtt_port"] | 0);
				}
		}

		// MQTT username
		if (doc.containsKey("mqtt_username")) {
				strlcpy(current_config->mqtt_username, doc["mqtt_username"] | "", CONFIG_MQTT_USERNAME_MAX_LEN);
		}

		// MQTT password (only update if provided and not empty)
		if (doc.containsKey("mqtt_password")) {
				const char* mqtt_pass = doc["mqtt_password"];
				if (mqtt_pass && strlen(mqtt_pass) > 0) {
						strlcpy(current_config->mqtt_password, mqtt_pass, CONFIG_MQTT_PASSWORD_MAX_LEN);
				}
		}


		// Power mode
		if (doc.containsKey("power_mode")) {
				strlcpy(current_config->power_mode, doc["power_mode"] | "always_on", CONFIG_POWER_MODE_MAX_LEN);
		}

		// Publish transport
		if (doc.containsKey("publish_transport")) {
				strlcpy(current_config->publish_transport, doc["publish_transport"] | "ble", CONFIG_PUBLISH_TRANSPORT_MAX_LEN);
		}

		// Cycle interval (legacy mqtt_interval_seconds supported as alias)
		const bool has_cycle_interval = doc.containsKey("cycle_interval_seconds");
		const bool has_mqtt_interval = doc.containsKey("mqtt_interval_seconds");
		if (has_cycle_interval || has_mqtt_interval) {
				const JsonVariant value = has_cycle_interval ? doc["cycle_interval_seconds"] : doc["mqtt_interval_seconds"];
				if (value.is<const char*>()) {
						const char* v = value;
						current_config->cycle_interval_seconds = (uint16_t)atoi(v ? v : "0");
				} else {
						current_config->cycle_interval_seconds = (uint16_t)(value | 0);
				}
		}

		// Portal idle timeout
		if (doc.containsKey("portal_idle_timeout_seconds")) {
				if (doc["portal_idle_timeout_seconds"].is<const char*>()) {
						const char* v = doc["portal_idle_timeout_seconds"];
						current_config->portal_idle_timeout_seconds = (uint16_t)atoi(v ? v : "0");
				} else {
						current_config->portal_idle_timeout_seconds = (uint16_t)(doc["portal_idle_timeout_seconds"] | 0);
				}
		}

		// WiFi backoff max
		if (doc.containsKey("wifi_backoff_max_seconds")) {
				if (doc["wifi_backoff_max_seconds"].is<const char*>()) {
						const char* v = doc["wifi_backoff_max_seconds"];
						current_config->wifi_backoff_max_seconds = (uint16_t)atoi(v ? v : "0");
				} else {
						current_config->wifi_backoff_max_seconds = (uint16_t)(doc["wifi_backoff_max_seconds"] | 0);
				}
		}

		// BLE timing settings
		if (doc.containsKey("ble_adv_burst_ms")) {
				if (doc["ble_adv_burst_ms"].is<const char*>()) {
						const char* v = doc["ble_adv_burst_ms"];
						current_config->ble_adv_burst_ms = (uint16_t)atoi(v ? v : "0");
				} else {
						current_config->ble_adv_burst_ms = (uint16_t)(doc["ble_adv_burst_ms"] | 0);
				}
		}

		if (doc.containsKey("ble_adv_gap_ms")) {
				if (doc["ble_adv_gap_ms"].is<const char*>()) {
						const char* v = doc["ble_adv_gap_ms"];
						current_config->ble_adv_gap_ms = (uint16_t)atoi(v ? v : "0");
				} else {
						current_config->ble_adv_gap_ms = (uint16_t)(doc["ble_adv_gap_ms"] | 0);
				}
		}

		if (doc.containsKey("ble_adv_bursts")) {
				if (doc["ble_adv_bursts"].is<const char*>()) {
						const char* v = doc["ble_adv_bursts"];
						current_config->ble_adv_bursts = (uint8_t)atoi(v ? v : "0");
				} else {
						current_config->ble_adv_bursts = (uint8_t)(doc["ble_adv_bursts"] | 0);
				}
		}

		if (doc.containsKey("ble_adv_interval_ms")) {
				if (doc["ble_adv_interval_ms"].is<const char*>()) {
						const char* v = doc["ble_adv_interval_ms"];
						current_config->ble_adv_interval_ms = (uint16_t)atoi(v ? v : "0");
				} else {
						current_config->ble_adv_interval_ms = (uint16_t)(doc["ble_adv_interval_ms"] | 0);
				}
		}

		// MQTT publish scope
		if (doc.containsKey("mqtt_publish_scope")) {
				strlcpy(current_config->mqtt_publish_scope, doc["mqtt_publish_scope"] | "sensors_only", CONFIG_MQTT_SCOPE_MAX_LEN);
		}

		// Basic Auth enabled
		if (doc.containsKey("basic_auth_enabled")) {
				if (doc["basic_auth_enabled"].is<const char*>()) {
						const char* v = doc["basic_auth_enabled"];
						current_config->basic_auth_enabled = (v && (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 || strcasecmp(v, "on") == 0));
				} else {
						current_config->basic_auth_enabled = (bool)(doc["basic_auth_enabled"] | false);
				}
		}

		// Basic Auth username
		if (doc.containsKey("basic_auth_username")) {
				strlcpy(current_config->basic_auth_username, doc["basic_auth_username"] | "", CONFIG_BASIC_AUTH_USERNAME_MAX_LEN);
		}

		// Basic Auth password (only update if provided and not empty)
		if (doc.containsKey("basic_auth_password")) {
				const char* pass = doc["basic_auth_password"];
				if (pass && strlen(pass) > 0) {
						strlcpy(current_config->basic_auth_password, pass, CONFIG_BASIC_AUTH_PASSWORD_MAX_LEN);
				}
		}

		// Display settings - backlight brightness (0-100%)
		if (doc.containsKey("backlight_brightness")) {
				uint8_t brightness;
				// Handle both string and integer values from form
				if (doc["backlight_brightness"].is<const char*>()) {
						const char* brightness_str = doc["backlight_brightness"];
						brightness = (uint8_t)atoi(brightness_str ? brightness_str : "100");
				} else {
						brightness = (uint8_t)(doc["backlight_brightness"] | 100);
				}

				if (brightness > 100) brightness = 100;
				current_config->backlight_brightness = brightness;

				LOGI("Config", "Backlight brightness set to %d%%", brightness);

				// Apply brightness immediately (will also be persisted when config saved)
				#if HAS_DISPLAY
				display_manager_set_backlight_brightness(brightness);

				// Edge case: if the device was in screen saver (backlight at 0), changing brightness
				// externally would light the screen without updating the screen saver state.
				// Treat this as explicit activity+wake so auto-sleep keeps working.
				screen_saver_manager_notify_activity(true);
				#endif
		}

		#if HAS_DISPLAY
		// Screen saver settings
		if (doc.containsKey("screen_saver_enabled")) {
				if (doc["screen_saver_enabled"].is<const char*>()) {
						const char* v = doc["screen_saver_enabled"];
						current_config->screen_saver_enabled = (v && (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 || strcasecmp(v, "on") == 0));
				} else {
						current_config->screen_saver_enabled = (bool)(doc["screen_saver_enabled"] | false);
				}
		}

		if (doc.containsKey("screen_saver_timeout_seconds")) {
				if (doc["screen_saver_timeout_seconds"].is<const char*>()) {
						const char* v = doc["screen_saver_timeout_seconds"];
						current_config->screen_saver_timeout_seconds = (uint16_t)atoi(v ? v : "0");
				} else {
						current_config->screen_saver_timeout_seconds = (uint16_t)(doc["screen_saver_timeout_seconds"] | 0);
				}
		}

		if (doc.containsKey("screen_saver_fade_out_ms")) {
				if (doc["screen_saver_fade_out_ms"].is<const char*>()) {
						const char* v = doc["screen_saver_fade_out_ms"];
						current_config->screen_saver_fade_out_ms = (uint16_t)atoi(v ? v : "0");
				} else {
						current_config->screen_saver_fade_out_ms = (uint16_t)(doc["screen_saver_fade_out_ms"] | 0);
				}
		}

		if (doc.containsKey("screen_saver_fade_in_ms")) {
				if (doc["screen_saver_fade_in_ms"].is<const char*>()) {
						const char* v = doc["screen_saver_fade_in_ms"];
						current_config->screen_saver_fade_in_ms = (uint16_t)atoi(v ? v : "0");
				} else {
						current_config->screen_saver_fade_in_ms = (uint16_t)(doc["screen_saver_fade_in_ms"] | 0);
				}
		}

		if (doc.containsKey("screen_saver_wake_on_touch")) {
				if (doc["screen_saver_wake_on_touch"].is<const char*>()) {
						const char* v = doc["screen_saver_wake_on_touch"];
						current_config->screen_saver_wake_on_touch = (v && (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 || strcasecmp(v, "on") == 0));
				} else {
						current_config->screen_saver_wake_on_touch = (bool)(doc["screen_saver_wake_on_touch"] | false);
				}
		}
		#endif

		current_config->magic = CONFIG_MAGIC;

		// Validate config
		if (!config_manager_is_valid(current_config)) {
				request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid configuration\"}");
				portENTER_CRITICAL(&g_config_post_mux);
				config_post_reset();
				portEXIT_CRITICAL(&g_config_post_mux);
				return;
		}

		// Save to NVS
		if (config_manager_save(current_config)) {
				LOGI("Portal", "Config saved");
				request->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration saved\"}");

				portENTER_CRITICAL(&g_config_post_mux);
				config_post_reset();
				portEXIT_CRITICAL(&g_config_post_mux);

				// Check for no_reboot parameter
				if (!request->hasParam("no_reboot")) {
						LOGI("Portal", "Rebooting device");
						// Schedule reboot after response is sent
						delay(100);
						ESP.restart();
				}
		} else {
				LOGE("Portal", "Config save failed");
				request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to save\"}");

				portENTER_CRITICAL(&g_config_post_mux);
				config_post_reset();
				portEXIT_CRITICAL(&g_config_post_mux);
		}
}

void handleDeleteConfig(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		if (config_manager_reset()) {
				request->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration reset\"}");

				// Schedule reboot after response is sent
				delay(100);
				ESP.restart();
		} else {
				request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to reset\"}");
		}
}
