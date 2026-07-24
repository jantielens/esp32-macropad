#pragma once

// ============================================================================
// Board Overrides: esp32s3-ble-bridge
// ============================================================================

// Dedicated mains-powered e-paper BLE assignment bridge.
#define IS_EPAPER_BLE_BRIDGE true

// Headless bridge: WiFi is provided by the core always-on path.
#define HAS_DISPLAY false
#define HAS_TOUCH false
#define HAS_AUDIO false
#define HAS_BLE_HID false
#define HAS_EPAPER false
#define HAS_BLE true

// Keep MQTT enabled unless the measured 4 MB image requires the documented
// size fallback.
#define HAS_MQTT true

// This board has no sensors or physical action buttons.
#define HAS_SENSOR_BME280 false
#define HAS_SENSOR_LD2410_OUT false
#define HAS_SENSOR_DUMMY false
#define HAS_BUTTON false

// Limit SRAM-backed MQTT trigger storage on the no-PSRAM target.
#define MAX_MQTT_TRIGGERS 3
