#pragma once
#include "Arduino.h"
#include "../log_manager.h"
#include "../freertos/portmacro.h"
#include "../esp_heap_caps.h"

// Only SDK-independent configuration fields consumed by these two modules.
#define BOARD_CONFIG_H
#define HAS_DISPLAY 0
#define WIFI_MAX_ATTEMPTS 3
#define WIFI_TIER1_DURATION_MS 5000
#define WIFI_TIER2_DURATION_MS 120000
#define WIFI_REBOOT_AFTER_MS 300000
#define WIFI_TIER2_BACKOFF_BASE_MS 5000
#define WIFI_TIER2_BACKOFF_MAX_MS 30000
#define WIFI_CACHED_RSSI_FLOOR_DBM -78
#define CONFIG_MANAGER_H
#define CONFIG_SSID_MAX_LEN 32
#define CONFIG_DEVICE_NAME_MAX_LEN 32
#define CONFIG_AP_PASSWORD_MAX_LEN 64
struct DeviceConfig {
    char wifi_ssid[CONFIG_SSID_MAX_LEN];
    char wifi_password[64];
    char ap_password[CONFIG_AP_PASSWORD_MAX_LEN];
    char device_name[CONFIG_DEVICE_NAME_MAX_LEN];
    char fixed_ip[16], subnet_mask[16], gateway[16], dns1[16], dns2[16];
};
inline void config_manager_sanitize_device_name(const char* name, char* out, size_t size) {
    strlcpy(out, name, size);
}
#define DEVICE_TELEMETRY_H
void device_telemetry_cache_rssi();
