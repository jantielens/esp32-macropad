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

#if HAS_AUDIO
#include "audio.h"
#endif

#include <ArduinoJson.h>
#include <WiFi.h>

#include <esp_heap_caps.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Parse a bool value from a JSON field that may arrive as string ("true"/"1"/"on") or native bool.
static bool parseBoolField(const JsonDocument &doc, const char* key) {
		if (doc[key].is<const char*>()) {
				const char* v = doc[key];
				return (v && (strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0 || strcasecmp(v, "on") == 0));
		}
		return (bool)(doc[key] | false);
}

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

				// MQTT settings (password not returned)
				(*doc)["mqtt_host"] = current_config->mqtt_host;
				(*doc)["mqtt_port"] = current_config->mqtt_port;
				(*doc)["mqtt_username"] = current_config->mqtt_username;
				(*doc)["mqtt_password"] = "";

				// Power settings
				(*doc)["operating_mode"] = current_config->operating_mode;
				(*doc)["duty_cycle_wake_seconds"] = current_config->duty_cycle_wake_seconds;
				(*doc)["mqtt_publish_interval_seconds"] = current_config->mqtt_publish_interval_seconds;
				(*doc)["portal_idle_timeout_seconds"] = current_config->portal_idle_timeout_seconds;
				(*doc)["wifi_backoff_max_seconds"] = current_config->wifi_backoff_max_seconds;

				#if HAS_BLE
				(*doc)["ble_burst_count"] = current_config->ble_burst_count;
				(*doc)["ble_adv_interval_ms"] = current_config->ble_adv_interval_ms;
				#endif

				// Build capability map so the portal UI can hide controls for
				// features that are not compiled into this firmware.
				JsonObject caps = (*doc).createNestedObject("caps");
				caps["ble"] = (bool)HAS_BLE;
				caps["mqtt"] = (bool)HAS_MQTT;
				caps["display"] = (bool)HAS_DISPLAY;
				caps["ble_hid"] = (bool)HAS_BLE_HID;
				caps["epaper"] = (bool)HAS_EPAPER;

				// MQTT scope
				(*doc)["mqtt_publish_scope"] = current_config->mqtt_publish_scope;

				#if HAS_EPAPER
				// E-paper image source (epaper_last_crc32 is internal state — not exposed)
				(*doc)["epaper_url"] = current_config->epaper_url;
				(*doc)["epaper_rotation"] = current_config->epaper_rotation;
				#endif

				// Web portal Basic Auth (password not returned)
				(*doc)["basic_auth_enabled"] = current_config->basic_auth_enabled;
				(*doc)["basic_auth_username"] = current_config->basic_auth_username;
				(*doc)["basic_auth_password"] = "";
				(*doc)["basic_auth_password_set"] = (strlen(current_config->basic_auth_password) > 0);

				// Display settings
				(*doc)["backlight_brightness"] = current_config->backlight_brightness;

				#if HAS_BLE_HID
				(*doc)["ble_enabled"] = current_config->ble_enabled;
				#endif

				#if HAS_AUDIO
				(*doc)["audio_volume"] = current_config->audio_volume;
				(*doc)["tap_beep"] = current_config->tap_beep;
				(*doc)["lp_beep"] = current_config->lp_beep;
				#endif

				#if HAS_DISPLAY
				// Screen saver settings
				(*doc)["screen_saver_enabled"] = current_config->screen_saver_enabled;
				(*doc)["screen_saver_timeout_seconds"] = current_config->screen_saver_timeout_seconds;
				(*doc)["screen_saver_fade_out_ms"] = current_config->screen_saver_fade_out_ms;
				(*doc)["screen_saver_fade_in_ms"] = current_config->screen_saver_fade_in_ms;
				(*doc)["screen_saver_wake_on_touch"] = current_config->screen_saver_wake_on_touch;
				(*doc)["screen_saver_wake_binding"] = current_config->screen_saver_wake_binding;
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

		// Security hardening: never allow changing Basic Auth settings in AP/core
		// mode AFTER initial setup. Otherwise, an attacker near the device could
		// wait for fallback AP mode and lock out the owner. EXCEPTION: during
		// true first-boot (no WiFi configured yet) the wizard legitimately needs
		// to set auth, so we only enforce the guard when an SSID is already on
		// file (i.e., this is a fallback AP session, not first-boot).
		bool first_boot = (current_config->wifi_ssid[0] == '\0');
		if (web_portal_is_ap_mode_active() && !first_boot && (doc.containsKey("basic_auth_enabled") || doc.containsKey("basic_auth_username") || doc.containsKey("basic_auth_password"))) {
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


		// Operating mode (transport + duty cycle selector)
		if (doc.containsKey("operating_mode")) {
				strlcpy(current_config->operating_mode, doc["operating_mode"] | "always_on", CONFIG_OPERATING_MODE_MAX_LEN);
		}

		// Duty-Cycle wake interval
		if (doc.containsKey("duty_cycle_wake_seconds")) {
				if (doc["duty_cycle_wake_seconds"].is<const char*>()) {
						const char* v = doc["duty_cycle_wake_seconds"];
						current_config->duty_cycle_wake_seconds = (uint16_t)atoi(v ? v : "0");
				} else {
						current_config->duty_cycle_wake_seconds = (uint16_t)(doc["duty_cycle_wake_seconds"] | 0);
				}
		}

		// MQTT publish interval (Always-On periodic publish; 0 = disabled)
		if (doc.containsKey("mqtt_publish_interval_seconds")) {
				if (doc["mqtt_publish_interval_seconds"].is<const char*>()) {
						const char* v = doc["mqtt_publish_interval_seconds"];
						current_config->mqtt_publish_interval_seconds = (uint16_t)atoi(v ? v : "0");
				} else {
						current_config->mqtt_publish_interval_seconds = (uint16_t)(doc["mqtt_publish_interval_seconds"] | 0);
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

		// MQTT publish scope
		if (doc.containsKey("mqtt_publish_scope")) {
				strlcpy(current_config->mqtt_publish_scope, doc["mqtt_publish_scope"] | "sensors_only", CONFIG_MQTT_SCOPE_MAX_LEN);
		}

		#if HAS_EPAPER
		if (doc.containsKey("epaper_url")) {
				strlcpy(current_config->epaper_url, doc["epaper_url"] | "", CONFIG_EPAPER_URL_MAX_LEN);
		}
		if (doc.containsKey("epaper_rotation")) {
				uint8_t r = doc["epaper_rotation"].is<const char*>()
						? (uint8_t)atoi(doc["epaper_rotation"] | "0")
						: (uint8_t)(doc["epaper_rotation"] | 0);
				if (r > 3) r = 0;
				current_config->epaper_rotation = r;
		}
		#endif

		#if HAS_BLE
		// BLE telemetry advertising burst count (1..255)
		if (doc.containsKey("ble_burst_count")) {
				uint16_t v = doc["ble_burst_count"].is<const char*>()
						? (uint16_t)atoi(doc["ble_burst_count"] | "0")
						: (uint16_t)(doc["ble_burst_count"] | 0);
				if (v == 0) v = BLE_TELEMETRY_DEFAULT_BURST_COUNT;
				if (v > 255) v = 255;
				current_config->ble_burst_count = (uint8_t)v;
		}

		// BLE telemetry advertising interval (ms)
		if (doc.containsKey("ble_adv_interval_ms")) {
				uint16_t v = doc["ble_adv_interval_ms"].is<const char*>()
						? (uint16_t)atoi(doc["ble_adv_interval_ms"] | "0")
						: (uint16_t)(doc["ble_adv_interval_ms"] | 0);
				if (v == 0) v = BLE_TELEMETRY_DEFAULT_ADV_INTERVAL_MS;
				current_config->ble_adv_interval_ms = v;
		}
		#endif

		// Basic Auth enabled
		if (doc.containsKey("basic_auth_enabled")) {
				current_config->basic_auth_enabled = parseBoolField(doc, "basic_auth_enabled");
		}

		#if HAS_BLE_HID
		if (doc.containsKey("ble_enabled")) {
				current_config->ble_enabled = parseBoolField(doc, "ble_enabled");
		}
		#endif

		#if HAS_AUDIO
		if (doc.containsKey("audio_volume")) {
				uint8_t vol;
				if (doc["audio_volume"].is<const char*>()) {
						vol = (uint8_t)atoi(doc["audio_volume"] | "70");
				} else {
						vol = (uint8_t)(doc["audio_volume"] | 70);
				}
				if (vol > 100) vol = 100;
				current_config->audio_volume = vol;
				audio_set_volume(vol);
		}
		if (doc.containsKey("tap_beep")) {
				strlcpy(current_config->tap_beep, doc["tap_beep"] | "", CONFIG_BEEP_PATTERN_MAX_LEN);
		}
		if (doc.containsKey("lp_beep")) {
				strlcpy(current_config->lp_beep, doc["lp_beep"] | "", CONFIG_BEEP_PATTERN_MAX_LEN);
		}
		#endif

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
				if (brightness < MIN_USER_BRIGHTNESS) brightness = MIN_USER_BRIGHTNESS;
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
				current_config->screen_saver_enabled = parseBoolField(doc, "screen_saver_enabled");
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
				current_config->screen_saver_wake_on_touch = parseBoolField(doc, "screen_saver_wake_on_touch");
		}

		if (doc.containsKey("screen_saver_wake_binding")) {
				strlcpy(current_config->screen_saver_wake_binding, doc["screen_saver_wake_binding"] | "", CONFIG_SS_WAKE_BINDING_MAX_LEN);
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

		// Send the success response FIRST so the client receives it before the
		// long blocking wipe (NVS erase + LittleFS.format() / SD rmrf, several
		// seconds total) starves the AsyncTCP task. The client interprets a
		// network failure here as success too, but delivering the JSON is the
		// happy path and makes debugging much easier.
		request->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration reset\"}");
		// Give AsyncWebServer time to flush the response before we block.
		delay(100);

		config_manager_factory_reset();
		ESP.restart();
}
