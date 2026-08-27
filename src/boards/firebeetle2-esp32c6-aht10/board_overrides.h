#pragma once

// DFRobot FireBeetle 2 ESP32-C6 v1.2 with an external AHT10 on GPIO19/20.
// GPIO0 is connected to the onboard LiPo battery divider (2:1).

#define HAS_DISPLAY false
#define HAS_TOUCH false
#define HAS_AUDIO false
#define HAS_BLE_HID false
#define HAS_BUILTIN_LED false

#define HAS_MQTT true
#define HAS_BLE true
#define HAS_STORAGE_BROWSER false

// Keep the OTA-only filesystem-less build small and suitable for the C6's RAM.
#define HTTP_STREAM_CHUNK_SIZE 1024
#define AP_MAX_CONNECTIONS 1
#define MAX_MQTT_TRIGGERS 3

// The built-in LED is GPIO15. HAS_BUILTIN_LED=false keeps it unconfigured.

#define HAS_SENSOR_AHT10 true
#define SENSOR_I2C_SDA 19
#define SENSOR_I2C_SCL 20

#define HAS_SENSOR_BATTERY_ADC true
#define BATTERY_ADC_PIN 0
#define BATTERY_ADC_DIVIDER 2.0f
#define BATTERY_ADC_CALIBRATION 1.0f
#define BATTERY_ADC_SAMPLE_COUNT 8

// GPIO9 remains available for boot-hold configuration mode.
#define HAS_BUTTON false
#define HAS_CONFIG_MODE_BUTTON true
#define BUTTON_PIN 9
#define BUTTON_ACTIVE_LOW true
#define POWERON_CONFIG_BURST_ENABLED true