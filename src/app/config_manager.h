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
 *   config_manager_reset();          // Erase all config
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

// Power settings
#define CONFIG_POWER_MODE_MAX_LEN 16
#define CONFIG_MQTT_SCOPE_MAX_LEN 20

// Screen saver MQTT wake binding
#define CONFIG_SS_WAKE_BINDING_MAX_LEN 192

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

		// Power settings
		char power_mode[CONFIG_POWER_MODE_MAX_LEN];            // always_on | duty_cycle | config | ap
		uint16_t cycle_interval_seconds;                       // default 120
		uint16_t portal_idle_timeout_seconds;                  // default 120
		uint16_t wifi_backoff_max_seconds;                     // default 900

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
		
		// Validation flag (magic number to detect valid config)
		uint32_t magic;
};

// Magic number for config validation
#define CONFIG_MAGIC 0xDEADBEEF

// API Functions
void config_manager_init();                           // Initialize NVS
bool config_manager_load(DeviceConfig *config);       // Load config from NVS
bool config_manager_save(const DeviceConfig *config); // Save config to NVS
bool config_manager_reset();                          // Erase config from NVS
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
