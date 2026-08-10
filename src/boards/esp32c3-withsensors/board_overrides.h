#pragma once

// ==========================================================================
// Board Overrides: esp32c3-withsensors
// ==========================================================================

// Enable MQTT (required for HA discovery in this sample when always-on or duty_cycle_mqtt)
#define HAS_MQTT true

// Headless board: no display, touch, audio, or BLE HID keyboard.
#define HAS_DISPLAY false
#define HAS_TOUCH false
#define HAS_AUDIO false
#define HAS_BLE_HID false

// The OTA-only partition table has no filesystem partition, so omit the
// unusable portal/MCP storage browser and preserve firmware space.
#define HAS_STORAGE_BROWSER false

// ESP32-C3 only has ~320 KB internal SRAM, most of it consumed by WiFi softAP
// + lwIP + AsyncTCP. Shrink the HTTP streaming chunk size so each TX pbuf
// allocation fits comfortably in the small fragmented DMA-internal heap that
// remains after softAP brings up its DHCP/DNS pools. Without this the
// portal assets stall mid-stream with `transport_drv_sta_tx` pbuf alloc
// failures (size ~2.3 KB requested vs ~1.6 KB largest free).
#define HTTP_STREAM_CHUNK_SIZE 1024

// Limit softAP to a single concurrent client. The default (4) reserves
// per-station buffers in DMA-internal RAM that we cannot spare on the C3.
// One client is sufficient for first-time provisioning.
#define AP_MAX_CONNECTIONS 1

// Enable BTHome v2 BLE telemetry as an alternative transport (duty_cycle_ble mode).
#define HAS_BLE true

// Enable user button (GPIO9 on ESP32-C3 Super Mini)
#define HAS_BUTTON true

// Hardware button actions can use pausable continuations. Keep one slot on
// this internal-RAM-constrained C3 target instead of the default three.
#define ACTION_CONTINUATION_SLOTS 1

// User button GPIO
#define BUTTON_PIN 9
// Button polarity (active-low)
#define BUTTON_ACTIVE_LOW true

// Hardware button actions: declare GPIO9 as a configurable action button.
// (BUTTON_PIN / BUTTON_ACTIVE_LOW above remain for boot-hold config-mode
// detection in check_config_mode_button(); these defs drive the runtime
// tap/hold action dispatcher.)
#define NUM_HW_BUTTONS 1
#ifdef __cplusplus
static constexpr HwButtonDef HW_BUTTON_DEFS[NUM_HW_BUTTONS] = {
    { .pin = 9, .active_low = true, .label = "BTN" }
};
#endif

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

// MQTT triggers: this board has no PSRAM, so the trigger config cache falls
// back to internal SRAM. Cap at 3 triggers (~4.3 KB) to limit SRAM cost.
#define MAX_MQTT_TRIGGERS 3
