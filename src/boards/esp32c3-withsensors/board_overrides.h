#pragma once

// ==========================================================================
// Board Overrides: esp32c3-withsensors
// ==========================================================================

// Enable MQTT (required for HA discovery in this sample)
#define HAS_MQTT true

// Enable user button (GPIO9 on ESP32-C3 Super Mini)
#define HAS_BUTTON true
// User button GPIO
#define BUTTON_PIN 9
// Button polarity (active-low)
#define BUTTON_ACTIVE_LOW true

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
