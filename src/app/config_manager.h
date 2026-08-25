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
#include "camera.h"

// Maximum string lengths
#define CONFIG_SSID_MAX_LEN 32
#define CONFIG_PASSWORD_MAX_LEN 64
#define CONFIG_DEVICE_NAME_MAX_LEN 32
#define CONFIG_IP_STR_MAX_LEN 16

// MQTT settings
#define CONFIG_MQTT_HOST_MAX_LEN 64
#define CONFIG_MQTT_USERNAME_MAX_LEN 32
#define CONFIG_MQTT_PASSWORD_MAX_LEN 64

// Home Assistant REST API (for ha_service button actions)
#define CONFIG_HA_URL_MAX_LEN 48
#define CONFIG_HA_TOKEN_MAX_LEN 184

// Operating mode (always_on | duty_cycle_mqtt | duty_cycle_ble | duty_cycle_epaper). Sized to fit longest value + NUL.
#define CONFIG_OPERATING_MODE_MAX_LEN 20
#define CONFIG_MQTT_SCOPE_MAX_LEN 20

// Screen saver MQTT wake binding
#define CONFIG_SS_WAKE_BINDING_MAX_LEN 192
#define CONFIG_IDLE_SCREEN_PAD_MAX_LEN 8

// Audio feedback beep pattern (may also be defined in pad_config.h)
#ifndef CONFIG_BEEP_PATTERN_MAX_LEN
#define CONFIG_BEEP_PATTERN_MAX_LEN 128
#endif

// Web portal Basic Auth (STA/full mode only)
#define CONFIG_BASIC_AUTH_USERNAME_MAX_LEN 32
#define CONFIG_BASIC_AUTH_PASSWORD_MAX_LEN 64

// MCP (Model Context Protocol) server bearer token.
// 32 lowercase hex chars (128-bit) + NUL; buffer sized to 40 per spec.
#if HAS_MCP
#define CONFIG_MCP_TOKEN_MAX_LEN 40
#endif

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

		// Home Assistant REST API (for ha_service button actions; independent of MQTT)
		char ha_url[CONFIG_HA_URL_MAX_LEN];      // e.g. http://192.168.1.50:8123 (empty = disabled)
		char ha_token[CONFIG_HA_TOKEN_MAX_LEN];  // HA long-lived access token

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

		// MCP server (Model Context Protocol; STA/full mode only)
#if HAS_MCP
		bool mcp_enabled;                              // default false (feature off)
		bool mcp_control_enabled;                      // default false (gates control tools)
		bool mcp_authoring_enabled;                    // default false (gates pad authoring/write tools)
		char mcp_token[CONFIG_MCP_TOKEN_MAX_LEN];      // bearer token; empty = none (fail closed)
#endif

#if HAS_BLE_HID
		// BLE Keyboard (runtime toggle; saves ~70 KB internal RAM when disabled)
		bool ble_enabled;                        // default false
#endif

#if HAS_AUDIO
		uint8_t audio_volume;                    // 0-100, default AUDIO_DEFAULT_VOLUME
		char tap_beep[CONFIG_BEEP_PATTERN_MAX_LEN];   // Beep DSL on tap (empty = disabled)
		char lp_beep[CONFIG_BEEP_PATTERN_MAX_LEN];    // Beep DSL on long-press (empty = disabled)
#endif

#if HAS_DISPLAY
		// Screen saver: optional Idle Screen followed by Display Sleep on inactivity.
		bool screen_saver_enabled;               // default true
		uint16_t screen_saver_timeout_seconds;   // default 300 (5 min)
		uint16_t screen_saver_fade_out_ms;       // default 800
		uint16_t screen_saver_fade_in_ms;        // default 400
		bool screen_saver_wake_on_touch;         // default true (when HAS_TOUCH)
		char screen_saver_wake_binding[CONFIG_SS_WAKE_BINDING_MAX_LEN]; // binding expression; wake on "ON"
		bool idle_screen_enabled;                // default false
		uint16_t idle_screen_timeout_seconds;    // default 300 (5 min)
		char idle_screen_pad[CONFIG_IDLE_SCREEN_PAD_MAX_LEN]; // transient pad shown while idle
#endif

#if HAS_CAMERA
		uint8_t camera_jpeg_quality;             // default CAMERA_JPEG_QUALITY_DEFAULT
		uint8_t camera_feed_target_fps;          // default CAMERA_FEED_TARGET_FPS_DEFAULT
		uint16_t camera_output_width;            // default CAMERA_OUTPUT_WIDTH_DEFAULT
		uint16_t camera_output_height;           // default CAMERA_OUTPUT_HEIGHT_DEFAULT
		uint16_t camera_exposure_lines;          // default CAMERA_EXPOSURE_LINES_DEFAULT
		uint16_t camera_white_balance_red_q8;    // default CAMERA_WHITE_BALANCE_Q8_DEFAULT
		uint16_t camera_white_balance_blue_q8;   // default CAMERA_WHITE_BALANCE_Q8_DEFAULT
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
