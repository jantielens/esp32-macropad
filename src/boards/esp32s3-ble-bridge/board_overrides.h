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

// AsyncTCP uses less than 1 KB on this headless bridge. Keeping its default
// 16 KB stack fragments internal RAM enough to prevent the 16.7 KB TLS input
// buffer from being allocated after BLE starts.
#define CONFIG_ASYNC_TCP_STACK_SIZE 8192

// Keep MQTT enabled unless the measured 4 MB image requires the documented
// size fallback.
#define HAS_MQTT true

// This board has no sensors or physical action buttons.
#define HAS_SENSOR_BME280 false
#define HAS_SENSOR_LD2410_OUT false
#define HAS_SENSOR_DUMMY false
#define HAS_BUTTON false

// Limit internal-RAM-backed MQTT trigger storage on the compact bridge target.
#define MAX_MQTT_TRIGGERS 3

// Promote bridge frame assignments to the primary portal page after the
// AP-mode WiFi setup wizard completes and the device joins the LAN.
#define PORTAL_PRIMARY_FRAGMENT "epaper-ble-bridge"
#define PORTAL_PRIMARY_CATEGORY "bridge"
#define PORTAL_PRIMARY_LABEL    "BLE Bridge"
#define PORTAL_PRIMARY_ICON     "\xf0\x9f\x93\xa1"
