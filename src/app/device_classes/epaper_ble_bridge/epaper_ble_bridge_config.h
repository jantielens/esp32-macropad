#ifndef EPAPER_BLE_BRIDGE_CONFIG_H
#define EPAPER_BLE_BRIDGE_CONFIG_H

#include <stddef.h>
#include <stdint.h>

class Preferences;

constexpr size_t EPAPER_BLE_BRIDGE_MAX_FRAMES = 2;
constexpr size_t EPAPER_BLE_BRIDGE_SITE_URL_MAX_LEN = 128;
constexpr size_t EPAPER_BLE_BRIDGE_DEVICE_ID_MAX_LEN = 64;
constexpr size_t EPAPER_BLE_BRIDGE_API_KEY_MAX_LEN = 65;
constexpr uint32_t EPAPER_BLE_BRIDGE_ADV_INTERVAL_MS = 100;
constexpr uint32_t EPAPER_BLE_FRAME_SCAN_DEADLINE_MS = 400;
constexpr uint32_t EPAPER_BLE_BRIDGE_FRESHNESS_LIMIT_MS = 15UL * 60UL * 1000UL;

struct EpaperBleBridgeFrameConfig {
    char site_url[EPAPER_BLE_BRIDGE_SITE_URL_MAX_LEN];
    char device_id[EPAPER_BLE_BRIDGE_DEVICE_ID_MAX_LEN];
    char api_key[EPAPER_BLE_BRIDGE_API_KEY_MAX_LEN];
};

struct EpaperBleBridgeConfig {
    uint8_t frame_count;
    EpaperBleBridgeFrameConfig frames[EPAPER_BLE_BRIDGE_MAX_FRAMES];
};

extern EpaperBleBridgeConfig g_epaper_ble_bridge_config;

void epaper_ble_bridge_config_defaults();
void epaper_ble_bridge_config_load(Preferences &preferences);
void epaper_ble_bridge_config_save(Preferences &preferences);

#endif
