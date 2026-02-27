#pragma once

// ==========================================================================
// Board Overrides: esp32c3-withsensors
// ==========================================================================

// Enable MQTT (required for HA discovery in this sample)
#define HAS_MQTT true

// Enable BLE (NimBLE)
#define HAS_BLE true

// Enable user button (GPIO9 on ESP32-C3 Super Mini)
#define HAS_BUTTON true
// User button GPIO
#define BUTTON_PIN 9
// Button polarity (active-low)
#define BUTTON_ACTIVE_LOW true

// NimBLE tuning (smaller footprint)
// NimBLE role: peripheral (required)
#define CONFIG_BT_NIMBLE_ROLE_PERIPHERAL 1
// NimBLE role: broadcaster
#define CONFIG_BT_NIMBLE_ROLE_BROADCASTER 1
// NimBLE role: central
#define CONFIG_BT_NIMBLE_ROLE_CENTRAL 0
// NimBLE role: observer
#define CONFIG_BT_NIMBLE_ROLE_OBSERVER 0
// NimBLE max connections
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 1
// NimBLE max bonded devices (tuning for small footprint)
#define CONFIG_BT_NIMBLE_MAX_BONDS 1
// NimBLE max CCCDs
#define CONFIG_BT_NIMBLE_MAX_CCCDS 4
// NimBLE msys1 block count
#define CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT 6
// NimBLE host log level
#define CONFIG_BT_NIMBLE_LOG_LEVEL 0
// NimBLE C++ wrapper log level
#define CONFIG_NIMBLE_CPP_LOG_LEVEL 0

// Enable BME280 sensor sample
#define HAS_SENSOR_BME280 false

// Enable LD2410 OUT pin presence sample
#define HAS_SENSOR_LD2410_OUT false

// Enable dummy sensor sample (synthetic value)
#define HAS_SENSOR_DUMMY true

// Enable power-on burst config trigger (no reliable user button)
#define POWERON_CONFIG_BURST_ENABLED true

// Sensor I2C pins (ESP32-C3 Super Mini defaults)
// Set to -1 to use Wire defaults if needed.
#define SENSOR_I2C_SDA 8
// SCL moved to GPIO10 to keep GPIO9 free for the user button.
#define SENSOR_I2C_SCL 10

// LD2410 OUT pin (presence)
#define LD2410_OUT_PIN 4

// Optional: BME280 address (0x76 or 0x77)
// #define BME280_I2C_ADDR 0x76
