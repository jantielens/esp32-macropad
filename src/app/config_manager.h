/*
 * Configuration Manager
 * 
 * Manages persistent storage of device configuration in ESP32 NVS.
 * Provides load/save/reset functionality with validation.
 * 
 * USAGE:
 *   config_manager_init();           // Initialize NVS
 *   if (config_manager_load()) {     // Try to load saved config
 *       // Config loaded, use it
 *   } else {
 *       // No config found, need to configure
 *   }
 *   config_manager_save();           // Save after user configures
 *   config_manager_factory_reset();  // Erase all config + filesystem data
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include "board_config.h"

// Maximum string lengths
#define CONFIG_SSID_MAX_LEN 32
#define CONFIG_PASSWORD_MAX_LEN 64
#define CONFIG_DEVICE_NAME_MAX_LEN 32
#define CONFIG_IP_STR_MAX_LEN 16

// MQTT settings
#define CONFIG_MQTT_HOST_MAX_LEN 64
#define CONFIG_MQTT_USERNAME_MAX_LEN 32
#define CONFIG_MQTT_PASSWORD_MAX_LEN 64

// Operating mode (always_on | duty_cycle_mqtt | duty_cycle_ble | duty_cycle_epaper). Sized to fit longest value + NUL.
#define CONFIG_OPERATING_MODE_MAX_LEN 20
#define CONFIG_MQTT_SCOPE_MAX_LEN 20

#if HAS_EPAPER
// E-Paper image URL (full HTTP/HTTPS). Sized to fit a realistic dashboard URL.
#define CONFIG_EPAPER_URL_MAX_LEN 256
#endif

// Screen saver MQTT wake binding
#define CONFIG_SS_WAKE_BINDING_MAX_LEN 192

// Audio feedback beep pattern (may also be defined in pad_config.h)
#ifndef CONFIG_BEEP_PATTERN_MAX_LEN
#define CONFIG_BEEP_PATTERN_MAX_LEN 128
#endif

// Web portal Basic Auth (STA/full mode only)
#define CONFIG_BASIC_AUTH_USERNAME_MAX_LEN 32
#define CONFIG_BASIC_AUTH_PASSWORD_MAX_LEN 64

// Configuration structure
struct DeviceConfig {
		// WiFi credentials
		char wifi_ssid[CONFIG_SSID_MAX_LEN];
		char wifi_password[CONFIG_PASSWORD_MAX_LEN];
		
		// Device settings
		char device_name[CONFIG_DEVICE_NAME_MAX_LEN];
		
		// Optional fixed IP configuration
		char fixed_ip[CONFIG_IP_STR_MAX_LEN];
		char subnet_mask[CONFIG_IP_STR_MAX_LEN];
		char gateway[CONFIG_IP_STR_MAX_LEN];
		char dns1[CONFIG_IP_STR_MAX_LEN];
		char dns2[CONFIG_IP_STR_MAX_LEN];

		// MQTT / Home Assistant integration settings (all optional)
		char mqtt_host[CONFIG_MQTT_HOST_MAX_LEN];
		uint16_t mqtt_port; // default to 1883 when mqtt_host set and mqtt_port is 0
		char mqtt_username[CONFIG_MQTT_USERNAME_MAX_LEN];
		char mqtt_password[CONFIG_MQTT_PASSWORD_MAX_LEN];

		// Operating mode (user-selectable transport / wake behaviour)
		char operating_mode[CONFIG_OPERATING_MODE_MAX_LEN];    // always_on | duty_cycle_mqtt | duty_cycle_ble
		uint16_t duty_cycle_wake_seconds;                      // default 120; deep-sleep duration in any duty-cycle mode (0 = wake immediately)
		uint16_t mqtt_publish_interval_seconds;                // default 120; periodic MQTT publish cadence in Always-On (0 = disabled)
		uint16_t portal_idle_timeout_seconds;                  // default 120; auto-sleep timeout in Config/AP mode
		uint16_t wifi_backoff_max_seconds;                     // default 900; max exponential backoff in duty_cycle_mqtt
#if HAS_BLE
		uint8_t ble_burst_count;                               // default BLE_TELEMETRY_DEFAULT_BURST_COUNT; advertising packets per wake
		uint16_t ble_adv_interval_ms;                          // default BLE_TELEMETRY_DEFAULT_ADV_INTERVAL_MS; ms between adv packets in a burst
#endif

		// MQTT scope
		char mqtt_publish_scope[CONFIG_MQTT_SCOPE_MAX_LEN];    // sensors_only | diagnostics_only | all
		
		// Display settings
		uint8_t backlight_brightness;  // 0-100%, default 100

		// Web portal Basic Auth (optional; enforced in STA/full mode only)
		bool basic_auth_enabled;
		char basic_auth_username[CONFIG_BASIC_AUTH_USERNAME_MAX_LEN];
		char basic_auth_password[CONFIG_BASIC_AUTH_PASSWORD_MAX_LEN];

#if HAS_BLE_HID
		// BLE Keyboard (runtime toggle; saves ~70 KB internal RAM when disabled)
		bool ble_enabled;                        // default false
#endif

#if HAS_AUDIO
		uint8_t audio_volume;                    // 0-100, default 70
		char tap_beep[CONFIG_BEEP_PATTERN_MAX_LEN];   // Beep DSL on tap (empty = disabled)
		char lp_beep[CONFIG_BEEP_PATTERN_MAX_LEN];    // Beep DSL on long-press (empty = disabled)
#endif

#if HAS_DISPLAY
		// Screen saver (burn-in prevention v1): backlight sleep on inactivity
		bool screen_saver_enabled;               // default false
		uint16_t screen_saver_timeout_seconds;   // default 300 (5 min)
		uint16_t screen_saver_fade_out_ms;       // default 800
		uint16_t screen_saver_fade_in_ms;        // default 400
		bool screen_saver_wake_on_touch;         // default true (when HAS_TOUCH)
		char screen_saver_wake_binding[CONFIG_SS_WAKE_BINDING_MAX_LEN]; // binding expression; wake on "ON"
#endif

#if HAS_EPAPER
		// E-Paper dashboard image
		char epaper_url[CONFIG_EPAPER_URL_MAX_LEN];   // full HTTP(S) URL of the dashboard image
		uint8_t epaper_rotation;                      // 0..3, default 0
		uint32_t epaper_last_crc32;                   // CRC32 of last successfully rendered image (0 = none)

		// On-image status overlay
		bool epaper_overlay_enabled;                  // default false
		uint8_t epaper_overlay_position;              // 0=TL, 1=TR, 2=BL, 3=BR (default 3)
		uint8_t epaper_overlay_color;                 // 0=black, 1=darkgray, 2=lightgray, 3=white (default 0)
		uint8_t epaper_overlay_items;                 // bitmask: 0x1=batt icon, 0x2=batt %, 0x4=time, 0x8=cycle ms
		// Frontlight (boards with HAS_EPAPER_FRONTLIGHT only — values are still
		// stored/exposed on other boards so the same NVS layout works across
		// upgrades, but the duty cycle ignores them).
		uint8_t epaper_frontlight_brightness;         // 0..63 (0 = disabled, default 0)
		uint16_t epaper_frontlight_duration_s;        // seconds after button wake (default 30)
#endif
		
		// Validation flag (magic number to detect valid config)
		uint32_t magic;
};

// Magic number for config validation
#define CONFIG_MAGIC 0xDEADBEEF

// API Functions
void config_manager_init();                           // Initialize NVS
bool config_manager_load(DeviceConfig *config);       // Load config from NVS
bool config_manager_save(const DeviceConfig *config); // Save config to NVS
bool config_manager_factory_reset();                  // Erase entire NVS partition + wipe user filesystem (pads, icons, sounds, indexed stores)
bool config_manager_is_valid(const DeviceConfig *config); // Check if config is valid
void config_manager_print(const DeviceConfig *config); // Debug print config
void config_manager_sanitize_device_name(const char *input, char *output, size_t max_len); // Sanitize name for mDNS
String config_manager_get_default_device_name();      // Get default device name with chip ID

#if HAS_BLE_HID
bool config_manager_get_ble_owner_claimed();            // Persistent "device has an owner" flag
bool config_manager_set_ble_owner_claimed(bool claimed);
bool config_manager_get_ble_owner_addr(char* out, size_t out_len); // Stored owner identity address
bool config_manager_set_ble_owner_addr(const char* addr);
#endif

#endif // CONFIG_MANAGER_H
