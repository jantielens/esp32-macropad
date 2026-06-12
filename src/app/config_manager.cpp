/*
 * Configuration Manager Implementation
 * 
 * Uses ESP32 Preferences library (NVS wrapper) for persistent storage.
 * Stores configuration in "device_cfg" namespace.
 */

#include "config_manager.h"
#include "board_config.h"
#include "device_class.h"
#include "class_branding.h"
#include "web_assets.h"
#include "log_manager.h"
#include "power_config.h"
#include "storage.h"
#include <Preferences.h>
#include <nvs_flash.h>

// NVS namespace
#define CONFIG_NAMESPACE "device_cfg"

// Preferences keys
#define KEY_WIFI_SSID      "wifi_ssid"
#define KEY_WIFI_PASS      "wifi_pass"
#define KEY_DEVICE_NAME    "device_name"
#define KEY_FIXED_IP       "fixed_ip"
#define KEY_SUBNET_MASK    "subnet_mask"
#define KEY_GATEWAY        "gateway"
#define KEY_DNS1           "dns1"
#define KEY_DNS2           "dns2"
#define KEY_MQTT_HOST      "mqtt_host"
#define KEY_MQTT_PORT      "mqtt_port"
#define KEY_MQTT_USER      "mqtt_user"
#define KEY_MQTT_PASS      "mqtt_pass"
#define KEY_HA_URL         "ha_url"
#define KEY_HA_TOKEN       "ha_token"
#define KEY_OPERATING_MODE "op_mode"
#define KEY_DC_WAKE        "dc_wake_s"
#define KEY_MQTT_PUB       "mqtt_pub_s"
#define KEY_PORTAL_IDLE    "portal_idle"
#define KEY_WIFI_BACKOFF_MAX "wifi_bomax"
#define KEY_MQTT_SCOPE     "mqtt_scope"
#define KEY_BACKLIGHT_BRIGHTNESS "bl_bright"

#if HAS_BLE
#define KEY_BLE_BURST_COUNT     "ble_brst"
#define KEY_BLE_ADV_INTERVAL_MS "ble_adv"
#endif

// Web portal Basic Auth
#define KEY_BASIC_AUTH_ENABLED "ba_en"
#define KEY_BASIC_AUTH_USER    "ba_user"
#define KEY_BASIC_AUTH_PASS    "ba_pass"
#if HAS_BLE_HID
#define KEY_BLE_ENABLED    "ble_en"
#define KEY_BLE_OWNER      "ble_owner"
#define KEY_BLE_OWNER_ADDR "ble_oaddr"
#endif
#if HAS_DISPLAY
#define KEY_SCREEN_SAVER_ENABLED "ss_en"
#define KEY_SCREEN_SAVER_TIMEOUT "ss_to"
#define KEY_SCREEN_SAVER_FADE_OUT "ss_fo"
#define KEY_SCREEN_SAVER_FADE_IN "ss_fi"
#define KEY_SCREEN_SAVER_WAKE_TOUCH "ss_wt"
#define KEY_SCREEN_SAVER_WAKE_BINDING "ss_wb"
#endif
#if HAS_AUDIO
#define KEY_AUDIO_VOLUME   "audio_vol"
#define KEY_TAP_BEEP       "tap_beep"
#define KEY_LP_BEEP        "lp_beep"
#endif
#define KEY_MAGIC          "magic"

static Preferences preferences;

// Initialize NVS
void config_manager_init() {
		LOGI("Config", "NVS init start");
		esp_err_t err = nvs_flash_init();
		if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
				LOGW("Config", "NVS init error (%d) - erasing NVS", (int)err);
				nvs_flash_erase();
				err = nvs_flash_init();
		}

		if (err != ESP_OK) {
				LOGE("Config", "NVS init FAILED (%d)", (int)err);
				return;
		}

		LOGI("Config", "NVS init OK");
}

// Get default device name with unique chip ID
String config_manager_get_default_device_name() {
		uint32_t chipId = 0;
		for (int i = 0; i < 17; i = i + 8) {
				chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
		}
		char name[40];
		snprintf(name, sizeof(name), "%s %04X", device_class_get_full_name(), (uint16_t)(chipId & 0xFFFF));
		return String(name);
}

// Sanitize device name for mDNS (lowercase, alphanumeric + hyphens only)
void config_manager_sanitize_device_name(const char *input, char *output, size_t max_len) {
		if (!input || !output || max_len == 0) return;
		
		size_t j = 0;
		for (size_t i = 0; input[i] != '\0' && j < max_len - 1; i++) {
				char c = input[i];
				
				// Convert to lowercase
				if (c >= 'A' && c <= 'Z') {
						c = c + ('a' - 'A');
				}
				
				// Keep alphanumeric and convert spaces/special chars to hyphens
				if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
						output[j++] = c;
				} else if (c == ' ' || c == '_' || c == '-') {
						// Don't add hyphen if previous char was already a hyphen
						if (j > 0 && output[j-1] != '-') {
								output[j++] = '-';
						}
				}
		}
		
		// Remove trailing hyphen if present
		if (j > 0 && output[j-1] == '-') {
				j--;
		}
		
		output[j] = '\0';
}

// Load configuration from NVS
bool config_manager_load(DeviceConfig *config) {
		if (!config) {
				LOGE("Config", "Load failed: NULL pointer");
				return false;
		}

		LOGI("Config", "Load start");

		if (!preferences.begin(CONFIG_NAMESPACE, true)) { // Read-only mode
				LOGE("Config", "Preferences begin failed");
				return false;
		}
		
		// Check magic number first
		uint32_t magic = preferences.getUInt(KEY_MAGIC, 0);
		if (magic != CONFIG_MAGIC) {
				preferences.end();
				LOGW("Config", "No config found");
				
				// Initialize defaults for fields that need sensible values even when no config exists
				config->backlight_brightness = 100;  // Default to full brightness
				config->mqtt_port = 0;

				strlcpy(config->operating_mode, "always_on", CONFIG_OPERATING_MODE_MAX_LEN);
				config->duty_cycle_wake_seconds = 120;
				config->mqtt_publish_interval_seconds = 120;
				config->portal_idle_timeout_seconds = CONFIG_DEFAULT_PORTAL_IDLE_SECONDS;
				config->wifi_backoff_max_seconds = 900;

				strlcpy(config->mqtt_publish_scope, "sensors_only", CONFIG_MQTT_SCOPE_MAX_LEN);

				#if HAS_BLE
				config->ble_burst_count = BLE_TELEMETRY_DEFAULT_BURST_COUNT;
				config->ble_adv_interval_ms = BLE_TELEMETRY_DEFAULT_ADV_INTERVAL_MS;
				#endif

				// Basic Auth defaults
				config->basic_auth_enabled = false;
				config->basic_auth_username[0] = '\0';
				config->basic_auth_password[0] = '\0';

				#if HAS_BLE_HID
				config->ble_enabled = false;
				#endif

				#if HAS_DISPLAY
				// Screen saver defaults
				config->screen_saver_enabled = false;
				config->screen_saver_timeout_seconds = 300;
				config->screen_saver_fade_out_ms = 800;
				config->screen_saver_fade_in_ms = 400;
				#if HAS_TOUCH
				config->screen_saver_wake_on_touch = true;
				#else
				config->screen_saver_wake_on_touch = false;
				#endif
				config->screen_saver_wake_binding[0] = '\0';
				#endif

				// Let registered device classes seed their own defaults.
				device_class_dispatch_config_defaults(config);
				
				return false;
		}
		
		// Load WiFi settings
		preferences.getString(KEY_WIFI_SSID, config->wifi_ssid, CONFIG_SSID_MAX_LEN);
		preferences.getString(KEY_WIFI_PASS, config->wifi_password, CONFIG_PASSWORD_MAX_LEN);
		
		// Load device settings
		String default_name = config_manager_get_default_device_name();
		preferences.getString(KEY_DEVICE_NAME, config->device_name, CONFIG_DEVICE_NAME_MAX_LEN);
		if (strlen(config->device_name) == 0) {
				strlcpy(config->device_name, default_name.c_str(), CONFIG_DEVICE_NAME_MAX_LEN);
		}
		
		// Load fixed IP settings
		preferences.getString(KEY_FIXED_IP, config->fixed_ip, CONFIG_IP_STR_MAX_LEN);
		preferences.getString(KEY_SUBNET_MASK, config->subnet_mask, CONFIG_IP_STR_MAX_LEN);
		preferences.getString(KEY_GATEWAY, config->gateway, CONFIG_IP_STR_MAX_LEN);
		preferences.getString(KEY_DNS1, config->dns1, CONFIG_IP_STR_MAX_LEN);
		preferences.getString(KEY_DNS2, config->dns2, CONFIG_IP_STR_MAX_LEN);

		// Load MQTT settings (all optional)
		preferences.getString(KEY_MQTT_HOST, config->mqtt_host, CONFIG_MQTT_HOST_MAX_LEN);
		config->mqtt_port = preferences.getUShort(KEY_MQTT_PORT, 0);
		preferences.getString(KEY_MQTT_USER, config->mqtt_username, CONFIG_MQTT_USERNAME_MAX_LEN);
		preferences.getString(KEY_MQTT_PASS, config->mqtt_password, CONFIG_MQTT_PASSWORD_MAX_LEN);

		// Load Home Assistant REST API settings (optional)
		preferences.getString(KEY_HA_URL, config->ha_url, CONFIG_HA_URL_MAX_LEN);
		preferences.getString(KEY_HA_TOKEN, config->ha_token, CONFIG_HA_TOKEN_MAX_LEN);

		// Load power settings
		preferences.getString(KEY_OPERATING_MODE, config->operating_mode, CONFIG_OPERATING_MODE_MAX_LEN);
		if (strlen(config->operating_mode) == 0) {
				strlcpy(config->operating_mode, "always_on", CONFIG_OPERATING_MODE_MAX_LEN);
		}

		config->duty_cycle_wake_seconds = preferences.getUShort(KEY_DC_WAKE, 120);
		config->mqtt_publish_interval_seconds = preferences.getUShort(KEY_MQTT_PUB, 120);
		config->portal_idle_timeout_seconds = preferences.getUShort(KEY_PORTAL_IDLE, CONFIG_DEFAULT_PORTAL_IDLE_SECONDS);
		config->wifi_backoff_max_seconds = preferences.getUShort(KEY_WIFI_BACKOFF_MAX, 900);

		preferences.getString(KEY_MQTT_SCOPE, config->mqtt_publish_scope, CONFIG_MQTT_SCOPE_MAX_LEN);
		if (strlen(config->mqtt_publish_scope) == 0) {
				strlcpy(config->mqtt_publish_scope, "sensors_only", CONFIG_MQTT_SCOPE_MAX_LEN);
		}

		#if HAS_BLE
		config->ble_burst_count = preferences.getUChar(KEY_BLE_BURST_COUNT, BLE_TELEMETRY_DEFAULT_BURST_COUNT);
		config->ble_adv_interval_ms = preferences.getUShort(KEY_BLE_ADV_INTERVAL_MS, BLE_TELEMETRY_DEFAULT_ADV_INTERVAL_MS);
		#endif
		
		// Load display settings
		config->backlight_brightness = preferences.getUChar(KEY_BACKLIGHT_BRIGHTNESS, 100);
		LOGI("Config", "Loaded brightness: %d%%", config->backlight_brightness);

		// Load Basic Auth settings
		config->basic_auth_enabled = preferences.getBool(KEY_BASIC_AUTH_ENABLED, false);
		preferences.getString(KEY_BASIC_AUTH_USER, config->basic_auth_username, CONFIG_BASIC_AUTH_USERNAME_MAX_LEN);
		preferences.getString(KEY_BASIC_AUTH_PASS, config->basic_auth_password, CONFIG_BASIC_AUTH_PASSWORD_MAX_LEN);

		#if HAS_BLE_HID
		config->ble_enabled = preferences.getBool(KEY_BLE_ENABLED, false);
		#endif

		#if HAS_AUDIO
		config->audio_volume = preferences.getUChar(KEY_AUDIO_VOLUME, 50);
		{
			size_t n = preferences.getString(KEY_TAP_BEEP, config->tap_beep, CONFIG_BEEP_PATTERN_MAX_LEN);
			if (n == 0) strlcpy(config->tap_beep, "500:40", CONFIG_BEEP_PATTERN_MAX_LEN);
		}
		{
			size_t n = preferences.getString(KEY_LP_BEEP, config->lp_beep, CONFIG_BEEP_PATTERN_MAX_LEN);
			if (n == 0) strlcpy(config->lp_beep, "500:40 60 1000:40", CONFIG_BEEP_PATTERN_MAX_LEN);
		}
		#endif

		#if HAS_DISPLAY
		// Load screen saver settings
		config->screen_saver_enabled = preferences.getBool(KEY_SCREEN_SAVER_ENABLED, false);
		config->screen_saver_timeout_seconds = preferences.getUShort(KEY_SCREEN_SAVER_TIMEOUT, 300);
		config->screen_saver_fade_out_ms = preferences.getUShort(KEY_SCREEN_SAVER_FADE_OUT, 800);
		config->screen_saver_fade_in_ms = preferences.getUShort(KEY_SCREEN_SAVER_FADE_IN, 400);
		#if HAS_TOUCH
		config->screen_saver_wake_on_touch = preferences.getBool(KEY_SCREEN_SAVER_WAKE_TOUCH, true);
		#else
		config->screen_saver_wake_on_touch = preferences.getBool(KEY_SCREEN_SAVER_WAKE_TOUCH, false);
		#endif
		preferences.getString(KEY_SCREEN_SAVER_WAKE_BINDING, config->screen_saver_wake_binding, CONFIG_SS_WAKE_BINDING_MAX_LEN);
		#endif

		// Let registered device classes load their own fields from the same
		// already-open namespace.
		device_class_dispatch_config_load(config, preferences);
		
		config->magic = magic;
		
		preferences.end();
		
		// Validate loaded config
		if (!config_manager_is_valid(config)) {
				LOGE("Config", "Invalid config");
				return false;
		}
		
		config_manager_print(config);
		LOGI("Config", "Load complete");
		return true;
}

// Save configuration to NVS
bool config_manager_save(const DeviceConfig *config) {
		if (!config) {
				LOGE("Config", "Save failed: NULL pointer");
				return false;
		}
		
		if (!config_manager_is_valid(config)) {
				LOGE("Config", "Save failed: Invalid config");
				return false;
		}

		LOGI("Config", "Save start");
		
		preferences.begin(CONFIG_NAMESPACE, false); // Read-write mode
		
		// Save WiFi settings
		preferences.putString(KEY_WIFI_SSID, config->wifi_ssid);
		preferences.putString(KEY_WIFI_PASS, config->wifi_password);
		
		// Save device settings
		preferences.putString(KEY_DEVICE_NAME, config->device_name);
		
		// Save fixed IP settings
		preferences.putString(KEY_FIXED_IP, config->fixed_ip);
		preferences.putString(KEY_SUBNET_MASK, config->subnet_mask);
		preferences.putString(KEY_GATEWAY, config->gateway);
		preferences.putString(KEY_DNS1, config->dns1);
		preferences.putString(KEY_DNS2, config->dns2);

		// Save MQTT settings
		preferences.putString(KEY_MQTT_HOST, config->mqtt_host);
		preferences.putUShort(KEY_MQTT_PORT, config->mqtt_port);
		preferences.putString(KEY_MQTT_USER, config->mqtt_username);
		preferences.putString(KEY_MQTT_PASS, config->mqtt_password);

		// Save Home Assistant REST API settings
		preferences.putString(KEY_HA_URL, config->ha_url);
		preferences.putString(KEY_HA_TOKEN, config->ha_token);

		// Save power settings
		preferences.putString(KEY_OPERATING_MODE, config->operating_mode);
		preferences.putUShort(KEY_DC_WAKE, config->duty_cycle_wake_seconds);
		preferences.putUShort(KEY_MQTT_PUB, config->mqtt_publish_interval_seconds);
		preferences.putUShort(KEY_PORTAL_IDLE, config->portal_idle_timeout_seconds);
		preferences.putUShort(KEY_WIFI_BACKOFF_MAX, config->wifi_backoff_max_seconds);

		preferences.putString(KEY_MQTT_SCOPE, config->mqtt_publish_scope);

		#if HAS_BLE
		preferences.putUChar(KEY_BLE_BURST_COUNT, config->ble_burst_count);
		preferences.putUShort(KEY_BLE_ADV_INTERVAL_MS, config->ble_adv_interval_ms);
		#endif

		// Save display settings
		LOGI("Config", "Saving brightness: %d%%", config->backlight_brightness);
		preferences.putUChar(KEY_BACKLIGHT_BRIGHTNESS, config->backlight_brightness);

		// Save Basic Auth settings
		preferences.putBool(KEY_BASIC_AUTH_ENABLED, config->basic_auth_enabled);
		preferences.putString(KEY_BASIC_AUTH_USER, config->basic_auth_username);
		preferences.putString(KEY_BASIC_AUTH_PASS, config->basic_auth_password);

		#if HAS_BLE_HID
		preferences.putBool(KEY_BLE_ENABLED, config->ble_enabled);
		#endif

		#if HAS_AUDIO
		preferences.putUChar(KEY_AUDIO_VOLUME, config->audio_volume);
		preferences.putString(KEY_TAP_BEEP, config->tap_beep);
		preferences.putString(KEY_LP_BEEP, config->lp_beep);
		#endif

		#if HAS_DISPLAY
		// Save screen saver settings
		preferences.putBool(KEY_SCREEN_SAVER_ENABLED, config->screen_saver_enabled);
		preferences.putUShort(KEY_SCREEN_SAVER_TIMEOUT, config->screen_saver_timeout_seconds);
		preferences.putUShort(KEY_SCREEN_SAVER_FADE_OUT, config->screen_saver_fade_out_ms);
		preferences.putUShort(KEY_SCREEN_SAVER_FADE_IN, config->screen_saver_fade_in_ms);
		preferences.putBool(KEY_SCREEN_SAVER_WAKE_TOUCH, config->screen_saver_wake_on_touch);
		preferences.putString(KEY_SCREEN_SAVER_WAKE_BINDING, config->screen_saver_wake_binding);
		#endif

		// Let registered device classes persist their own fields.
		device_class_dispatch_config_save(config, preferences);
		
		// Save magic number last (indicates valid config)
		preferences.putUInt(KEY_MAGIC, CONFIG_MAGIC);
		
		preferences.end();
		
		config_manager_print(config);
		LOGI("Config", "Save complete");
		return true;
}

// Recursively remove a directory tree on the active Storage backend.
// Returns true if `path` no longer exists (or never did) after the call.
// Individual remove/rmdir failures are logged.
static bool factory_reset_rmrf(const char* path) {
		if (!path || !*path) return true;
		if (!Storage.exists(path)) return true;

		File root = Storage.open(path);
		if (!root) {
				LOGW("Config", "rmrf: cannot open %s", path);
				return false;
		}
		if (!root.isDirectory()) {
				root.close();
				if (Storage.remove(path)) return true;
				LOGW("Config", "rmrf: failed to remove file %s", path);
				return false;
		}

		bool ok = true;
		File entry = root.openNextFile();
		while (entry) {
				// Build the child's full path. Some FS impls return absolute names
				// from entry.name(); strip a leading slash so we don't get "//foo".
				const char* name = entry.name();
				if (name && name[0] == '/') name++;
				String child(path);
				if (!child.endsWith("/")) child += "/";
				child += (name ? name : "");

				bool is_dir = entry.isDirectory();
				entry.close();
				if (is_dir) {
						if (!factory_reset_rmrf(child.c_str())) ok = false;
				} else {
						if (!Storage.remove(child.c_str())) {
								LOGW("Config", "rmrf: failed to remove %s", child.c_str());
								ok = false;
						}
				}
				entry = root.openNextFile();
		}
		root.close();

		if (!Storage.rmdir(path)) {
				LOGW("Config", "rmrf: failed to rmdir %s", path);
				ok = false;
		}
		return ok;
}

// Factory reset: erase the entire NVS partition and wipe user data on the
// filesystem (pad configs, button defaults, timer/swipe/boot actions, icons,
// sounds, indexed stores). Caller is expected to reboot immediately after.
// Returns true only if BOTH the NVS erase and the filesystem wipe succeeded.
bool config_manager_factory_reset() {
		LOGI("Config", "Factory reset: erasing NVS partition");
		esp_err_t err = nvs_flash_erase();
		if (err != ESP_OK) {
				LOGE("Config", "nvs_flash_erase failed (%d)", (int)err);
		}
		// Re-init NVS so any code path that runs before the imminent reboot
		// (logging, BLE deinit, etc.) does not crash on a closed partition.
		nvs_flash_init();

		LOGI("Config", "Factory reset: wiping filesystem");
		bool fs_ok = true;
#if USE_SD_STORAGE
		// SD: cannot safely format from firmware. Selectively remove the
		// directories the firmware owns; leave any user files at root alone.
		fs_ok &= factory_reset_rmrf("/config");
		fs_ok &= factory_reset_rmrf("/icons");
		fs_ok &= factory_reset_rmrf("/sounds");
		fs_ok &= factory_reset_rmrf("/storage");
#else
		// LittleFS: format wipes everything in the data partition cleanly.
		if (!LittleFS.format()) {
				LOGE("Config", "LittleFS.format() failed");
				fs_ok = false;
		} else {
				LOGI("Config", "LittleFS formatted");
		}
#endif

		const bool ok = (err == ESP_OK) && fs_ok;
		LOGI("Config", "Factory reset %s", ok ? "complete" : "FAILED (partial wipe)");
		return ok;
}

#if HAS_BLE_HID
bool config_manager_get_ble_owner_claimed() {
		if (!preferences.begin(CONFIG_NAMESPACE, true)) {
				LOGE("Config", "Failed to open NVS for BLE owner read");
				return false;
		}
		const bool claimed = preferences.getBool(KEY_BLE_OWNER, false);
		preferences.end();
		return claimed;
}

bool config_manager_set_ble_owner_claimed(bool claimed) {
		if (!preferences.begin(CONFIG_NAMESPACE, false)) {
				LOGE("Config", "Failed to open NVS for BLE owner write");
				return false;
		}
		const bool ok = preferences.putBool(KEY_BLE_OWNER, claimed);
		preferences.end();
		return ok;
}

bool config_manager_get_ble_owner_addr(char* out, size_t out_len) {
		if (!out || out_len == 0) return false;
		out[0] = '\0';
		if (!preferences.begin(CONFIG_NAMESPACE, true)) {
				LOGE("Config", "Failed to open NVS for BLE owner addr read");
				return false;
		}
		preferences.getString(KEY_BLE_OWNER_ADDR, out, out_len);
		preferences.end();
		return out[0] != '\0';
}

bool config_manager_set_ble_owner_addr(const char* addr) {
		if (!preferences.begin(CONFIG_NAMESPACE, false)) {
				LOGE("Config", "Failed to open NVS for BLE owner addr write");
				return false;
		}
		const bool ok = preferences.putString(KEY_BLE_OWNER_ADDR, addr ? addr : "");
		preferences.end();
		return ok;
}

#endif

// Check if configuration is valid
bool config_manager_is_valid(const DeviceConfig *config) {
		if (!config) return false;
		if (config->magic != CONFIG_MAGIC) return false;
		if (strlen(config->device_name) == 0) return false;

		const PowerMode mode = power_config_parse_power_mode(config);
		const bool needs_wifi = (mode != PowerMode::DutyCycle && mode != PowerMode::DutyCycleBle);

		if (needs_wifi && strlen(config->wifi_ssid) == 0) return false;

		if (config->basic_auth_enabled) {
				if (strlen(config->basic_auth_username) == 0) return false;
				if (strlen(config->basic_auth_password) == 0) return false;
		}
		return true;
}

// Print configuration (for debugging)
void config_manager_print(const DeviceConfig *config) {
		if (!config) return;
		
		LOGI("Config", "Device: %s", config->device_name);
		
		// Show sanitized name for mDNS
		char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
		config_manager_sanitize_device_name(config->device_name, sanitized, CONFIG_DEVICE_NAME_MAX_LEN);
		LOGI("Config", "mDNS: %s.local", sanitized);
		
		LOGI("Config", "WiFi SSID: %s", config->wifi_ssid);
		LOGI("Config", "WiFi Pass: %s", strlen(config->wifi_password) > 0 ? "***" : "(none)");
		
		if (strlen(config->fixed_ip) > 0) {
				LOGI("Config", "IP: %s", config->fixed_ip);
				LOGI("Config", "Subnet: %s", config->subnet_mask);
				LOGI("Config", "Gateway: %s", config->gateway);
				LOGI("Config", "DNS: %s, %s", config->dns1, strlen(config->dns2) > 0 ? config->dns2 : "(none)");
		} else {
				LOGI("Config", "IP: DHCP");
		}

LOGI("Config", "Power: mode=%s dc_wake=%us idle=%us backoff_max=%us",
			config->operating_mode,
			(unsigned)config->duty_cycle_wake_seconds,
			(unsigned)config->portal_idle_timeout_seconds,
			(unsigned)config->wifi_backoff_max_seconds
		);

		LOGI("Config", "MQTT scope: %s", config->mqtt_publish_scope);

#if HAS_MQTT
		if (strlen(config->mqtt_host) > 0) {
				uint16_t port = config->mqtt_port > 0 ? config->mqtt_port : 1883;
				if (config->mqtt_publish_interval_seconds > 0) {
						LOGI("Config", "MQTT: %s:%d (publish interval %us)", config->mqtt_host, port, (unsigned)config->mqtt_publish_interval_seconds);
				} else {
						LOGI("Config", "MQTT: %s:%d (publish disabled)", config->mqtt_host, port);
				}
				LOGI("Config", "MQTT User: %s", strlen(config->mqtt_username) > 0 ? config->mqtt_username : "(none)");
				LOGI("Config", "MQTT Pass: %s", strlen(config->mqtt_password) > 0 ? "***" : "(none)");
		} else {
				LOGI("Config", "MQTT: disabled");
		}
#else
		// MQTT config can still exist in NVS, but the firmware has MQTT support compiled out.
		LOGI("Config", "MQTT: disabled (feature not compiled into firmware)");
#endif

		// Home Assistant REST API (independent of MQTT)
		if (strlen(config->ha_url) > 0) {
				LOGI("Config", "HA URL: %s", config->ha_url);
				LOGI("Config", "HA Token: %s", strlen(config->ha_token) > 0 ? "***" : "(none)");
		} else {
				LOGI("Config", "HA REST: disabled");
		}

#if HAS_BLE_HID
		LOGI("Config", "BLE Keyboard: %s", config->ble_enabled ? "enabled" : "disabled");
#endif

#if HAS_AUDIO
		LOGI("Config", "Audio volume: %u%%", config->audio_volume);
		if (config->tap_beep[0]) LOGI("Config", "Tap beep: %s", config->tap_beep);
		if (config->lp_beep[0]) LOGI("Config", "LP beep: %s", config->lp_beep);
#endif

#if HAS_DISPLAY
		if (strlen(config->screen_saver_wake_binding) > 0) {
				LOGI("Config", "SS wake binding: %s", config->screen_saver_wake_binding);
		}
#endif
}
