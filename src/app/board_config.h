#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// ============================================================================
// Board Configuration - Two-Phase Include Pattern
// ============================================================================
// This file provides default configuration for all boards using a two-phase
// include pattern:
//
// Phase 1: Load board-specific overrides first (if they exist)
// Phase 2: Define defaults using #ifndef guards (only if not already defined)
//
// To customize for a specific board, create: src/boards/[board-name]/board_overrides.h
// The build system will automatically detect and include board-specific overrides.
//
// Example board-specific override:
//   src/boards/cyd-v2/board_overrides.h

// ============================================================================
// Phase 1: Load Board-Specific Overrides
// ============================================================================
// build.sh defines BOARD_HAS_OVERRIDE when a board override directory exists
// and adds that directory to the include path. Board-specific settings are
// loaded first so they can override the defaults below.

#ifdef BOARD_HAS_OVERRIDE
#include "board_overrides.h"
#endif

// ============================================================================
// Project Branding
// ============================================================================
// Human-friendly project name used in the web UI and device name (can be set by build system).
#ifndef PROJECT_DISPLAY_NAME
#define PROJECT_DISPLAY_NAME "ESP32 Device"
#endif

// ============================================================================
// Phase 2: Default Hardware Capabilities
// ============================================================================
// These defaults are only applied if not already defined by board overrides.

// Enable built-in status LED support.
#ifndef HAS_BUILTIN_LED
#define HAS_BUILTIN_LED false
#endif

// Enable BLE (NimBLE) advertising support.
#ifndef HAS_BLE
#define HAS_BLE false
#endif

// Enable MQTT and Home Assistant integration.
#ifndef HAS_MQTT
#define HAS_MQTT true
#endif

// GPIO for the built-in LED (only used when HAS_BUILTIN_LED is true).
#ifndef LED_PIN
#define LED_PIN 2  // Common GPIO for ESP32 boards
#endif

// LED polarity: true if HIGH turns the LED on.
#ifndef LED_ACTIVE_HIGH
#define LED_ACTIVE_HIGH true  // true = HIGH turns LED on, false = LOW turns LED on
#endif

// ============================================================================
// Default WiFi Configuration
// ============================================================================

// Maximum WiFi connection attempts at boot before falling back.
#ifndef WIFI_MAX_ATTEMPTS
#define WIFI_MAX_ATTEMPTS 3
#endif

// ============================================================================
// Additional Default Configuration Settings
// ============================================================================
// Add new hardware features here using #ifndef guards to allow board-specific
// overrides.
//
// Usage Pattern in Application Code:
//   1. Define capabilities in board_overrides.h: #define HAS_BUTTON true
//   2. Use conditional compilation in app.ino:
//
//      #if HAS_BUTTON
//        pinMode(BUTTON_PIN, INPUT_PULLUP);
//        // Button-specific code only compiled when HAS_BUTTON is true
//      #endif
//
// Examples:
//
// Button:
// #ifndef HAS_BUTTON
// #define HAS_BUTTON false
// #endif
//
// #ifndef BUTTON_PIN
// #define BUTTON_PIN 0
// #endif
//
// Battery Monitor:
// #ifndef HAS_BATTERY_MONITOR
// #define HAS_BATTERY_MONITOR false
// #endif
//
// #ifndef BATTERY_ADC_PIN
// #define BATTERY_ADC_PIN 34
// #endif
//
// Display:
// #ifndef HAS_DISPLAY
// #define HAS_DISPLAY false
// #endif

// ============================================================================
// User Button (optional)
// ============================================================================
#ifndef HAS_BUTTON
#define HAS_BUTTON false
#endif

// GPIO pin for the optional user button (active level defined below).
#ifndef BUTTON_PIN
#define BUTTON_PIN 0
#endif

// Button polarity: true when pressed = LOW.
#ifndef BUTTON_ACTIVE_LOW
#define BUTTON_ACTIVE_LOW true
#endif

// Enable power-on burst detection to force Config Mode (NVS-backed, disabled by default).
// Intended for boards WITHOUT a reliable user button.
#ifndef POWERON_CONFIG_BURST_ENABLED
#define POWERON_CONFIG_BURST_ENABLED false
#endif

// ============================================================================
// Sensors (Optional)
// ============================================================================
// Enable BME280 (I2C) environmental sensor adapter.
#ifndef HAS_SENSOR_BME280
#define HAS_SENSOR_BME280 false
#endif

// Enable LD2410 OUT pin presence sensor adapter.
#ifndef HAS_SENSOR_LD2410_OUT
#define HAS_SENSOR_LD2410_OUT false
#endif

// Enable dummy sensor adapter (synthetic values for testing).
#ifndef HAS_SENSOR_DUMMY
#define HAS_SENSOR_DUMMY false
#endif

// I2C pins for sensors. Use -1 to keep default Wire pins.
#ifndef SENSOR_I2C_SDA
#define SENSOR_I2C_SDA -1
#endif

// I2C SCL pin for sensors.
#ifndef SENSOR_I2C_SCL
#define SENSOR_I2C_SCL -1
#endif

// I2C clock for sensors (Hz).
#ifndef SENSOR_I2C_FREQUENCY
#define SENSOR_I2C_FREQUENCY 400000
#endif

// BME280 I2C address (0x76 or 0x77).
#ifndef BME280_I2C_ADDR
#define BME280_I2C_ADDR 0x76
#endif

// LD2410 OUT pin (presence). Use -1 to disable.
#ifndef LD2410_OUT_PIN
#define LD2410_OUT_PIN -1
#endif

// Debounce for LD2410 OUT edge changes (ms).
#ifndef LD2410_OUT_DEBOUNCE_MS
#define LD2410_OUT_DEBOUNCE_MS 50
#endif

// ============================================================================
// Web Portal Health Widget
// ============================================================================
// How often the web UI polls /api/health.
#ifndef HEALTH_POLL_INTERVAL_MS
#define HEALTH_POLL_INTERVAL_MS 5000
#endif

// How much client-side history (sparklines) to keep.
#ifndef HEALTH_HISTORY_SECONDS
#define HEALTH_HISTORY_SECONDS 300
#endif

// ============================================================================
// Optional: Device-side Health History (/api/health/history)
// ============================================================================
// When enabled, firmware keeps a fixed-size ring buffer for sparklines so the
// portal can render history even when no client was connected.
// Default: enabled.
// Master switch for background telemetry tasks (CPU monitor, health-window
// timer, tripwires).  Set to 0 on boards where these tasks interfere with
// real-time display rendering.  /api/health still works — it just returns
// point-in-time values without min/max window bands or CPU %.
#ifndef DEVICE_TELEMETRY_BACKGROUND_TASKS
#define DEVICE_TELEMETRY_BACKGROUND_TASKS 1
#endif

// Enable device-side health history ring buffer for charting in the web portal
#ifndef HEALTH_HISTORY_ENABLED
#define HEALTH_HISTORY_ENABLED 1
#endif

// Sampling cadence for the device-side history (ms). Default aligns with UI poll.
#ifndef HEALTH_HISTORY_PERIOD_MS
#define HEALTH_HISTORY_PERIOD_MS 5000
#endif

#if HEALTH_HISTORY_ENABLED
// Derived number of samples.
#ifndef HEALTH_HISTORY_SAMPLES
#define HEALTH_HISTORY_SAMPLES ((HEALTH_HISTORY_SECONDS * 1000) / HEALTH_HISTORY_PERIOD_MS)
#endif

// Guardrails (must compile in both C and C++ translation units).
#if (HEALTH_HISTORY_PERIOD_MS < 1000)
#error HEALTH_HISTORY_PERIOD_MS too small
#endif

#if (((HEALTH_HISTORY_SECONDS * 1000UL) % (HEALTH_HISTORY_PERIOD_MS)) != 0)
#error HEALTH_HISTORY_SECONDS must be divisible by HEALTH_HISTORY_PERIOD_MS
#endif

#if (HEALTH_HISTORY_SAMPLES < 10)
#error HEALTH_HISTORY_SAMPLES too small
#endif

#if (HEALTH_HISTORY_SAMPLES > 600)
#error HEALTH_HISTORY_SAMPLES too large
#endif
#endif

// ============================================================================
// Display Configuration
// ============================================================================
// Enable display + LVGL UI support.
#ifndef HAS_DISPLAY
#define HAS_DISPLAY false
#endif

// Display driver selection
// Available drivers:
//   DISPLAY_DRIVER_TFT_ESPI (1) - Bodmer's TFT_eSPI (supports ILI9341, ST7789, etc.)
//   DISPLAY_DRIVER_LOVYANGFX (3) - LovyanGFX (future support)
//   DISPLAY_DRIVER_ARDUINO_GFX (4) - Arduino_GFX (QSPI displays like AXS15231B)
//   DISPLAY_DRIVER_ST7701_RGB (6) - Arduino_GFX ST7701 RGB panel (ESP32-4848S040)
//   DISPLAY_DRIVER_ARDUINO_GFX_ST77916 (7) - Arduino_GFX ST77916 QSPI 360x360 (JC3636W518)
//   DISPLAY_DRIVER_ST7703_DSI (8) - Direct ESP-IDF ST7703 MIPI-DSI (ESP32-P4-WIFI6-Touch-LCD-4B)
//   DISPLAY_DRIVER_ST7701_DSI (9) - Direct ESP-IDF ST7701 MIPI-DSI (JC4880P433, ESP32-P4)
#define DISPLAY_DRIVER_TFT_ESPI 1
#define DISPLAY_DRIVER_LOVYANGFX 3
#define DISPLAY_DRIVER_ARDUINO_GFX 4
#define DISPLAY_DRIVER_ST7701_RGB 6
#define DISPLAY_DRIVER_ARDUINO_GFX_ST77916 7
#define DISPLAY_DRIVER_ST7703_DSI 8
#define DISPLAY_DRIVER_ST7701_DSI 9

// Select the display HAL backend (one of the DISPLAY_DRIVER_* constants).
#ifndef DISPLAY_DRIVER
#define DISPLAY_DRIVER DISPLAY_DRIVER_TFT_ESPI  // Default to TFT_eSPI
#endif

// DPI pixel clock in Hz for ST7703 MIPI-DSI panels (ESP32-P4 only).
#ifndef ST7703_DPI_CLK_HZ
#define ST7703_DPI_CLK_HZ 38000000L
#endif

// MIPI-DSI lane bit rate in Mbps for ST7703 panels (ESP32-P4 only).
#ifndef ST7703_LANE_BIT_RATE
#define ST7703_LANE_BIT_RATE 480
#endif

// DSI timing defaults for ST7703 panels (ESP32-P4 only).
// Values from Waveshare BSP, validated on hardware.
// HSYNC pulse width in pixel clocks.
#ifndef ST7703_HSYNC_PULSE_WIDTH
#define ST7703_HSYNC_PULSE_WIDTH 20
#endif
// HSYNC back porch in pixel clocks.
#ifndef ST7703_HSYNC_BACK_PORCH
#define ST7703_HSYNC_BACK_PORCH 50
#endif
// HSYNC front porch in pixel clocks.
#ifndef ST7703_HSYNC_FRONT_PORCH
#define ST7703_HSYNC_FRONT_PORCH 50
#endif
// VSYNC pulse width in lines.
#ifndef ST7703_VSYNC_PULSE_WIDTH
#define ST7703_VSYNC_PULSE_WIDTH 4
#endif
// VSYNC back porch in lines.
#ifndef ST7703_VSYNC_BACK_PORCH
#define ST7703_VSYNC_BACK_PORCH 20
#endif
// VSYNC front porch in lines.
#ifndef ST7703_VSYNC_FRONT_PORCH
#define ST7703_VSYNC_FRONT_PORCH 20
#endif

// DSI timing defaults for ST7701 MIPI-DSI panels (ESP32-P4, direct ESP-IDF).
// Values from Arduino_GFX JC4880P433 example + GUITION BSP, validated on hardware.
// DPI pixel clock in Hz.
#ifndef ST7701_DSI_DPI_CLK_HZ
#define ST7701_DSI_DPI_CLK_HZ 34000000L
#endif
// MIPI-DSI lane bit rate in Mbps.
#ifndef ST7701_DSI_LANE_BIT_RATE
#define ST7701_DSI_LANE_BIT_RATE 500
#endif
// HSYNC pulse width in pixel clocks.
#ifndef ST7701_DSI_HSYNC_PULSE_WIDTH
#define ST7701_DSI_HSYNC_PULSE_WIDTH 12
#endif
// HSYNC back porch in pixel clocks.
#ifndef ST7701_DSI_HSYNC_BACK_PORCH
#define ST7701_DSI_HSYNC_BACK_PORCH 42
#endif
// HSYNC front porch in pixel clocks.
#ifndef ST7701_DSI_HSYNC_FRONT_PORCH
#define ST7701_DSI_HSYNC_FRONT_PORCH 42
#endif
// VSYNC pulse width in lines.
#ifndef ST7701_DSI_VSYNC_PULSE_WIDTH
#define ST7701_DSI_VSYNC_PULSE_WIDTH 2
#endif
// VSYNC back porch in lines.
#ifndef ST7701_DSI_VSYNC_BACK_PORCH
#define ST7701_DSI_VSYNC_BACK_PORCH 8
#endif
// VSYNC front porch in lines.
#ifndef ST7701_DSI_VSYNC_FRONT_PORCH
#define ST7701_DSI_VSYNC_FRONT_PORCH 166
#endif

// ============================================================================
// LVGL Configuration
// ============================================================================
// LVGL draw buffer size in pixels (larger = faster, more RAM).
#ifndef LVGL_BUFFER_SIZE
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 10)  // 10 lines buffer
#endif

// LVGL tick period in milliseconds.
#ifndef LVGL_TICK_PERIOD_MS
#define LVGL_TICK_PERIOD_MS 5
#endif

// Core to pin the LVGL render task to on dual-core chips (0 or 1).
#ifndef LVGL_TASK_CORE
#define LVGL_TASK_CORE 0
#endif

// FreeRTOS priority for the LVGL render task (1-24, higher = more CPU time).
// Default 4 matches ESP-IDF BSP convention; keeps rendering above WiFi (pri 2-3).
#ifndef LVGL_TASK_PRIORITY
#define LVGL_TASK_PRIORITY 4
#endif

// ============================================================================
// Backlight Configuration
// ============================================================================
// Enable backlight control (typically via PWM).
#ifndef HAS_BACKLIGHT
#define HAS_BACKLIGHT false
#endif

// LEDC channel used for backlight PWM.
#ifndef TFT_BACKLIGHT_PWM_CHANNEL
#define TFT_BACKLIGHT_PWM_CHANNEL 0  // LEDC channel for PWM control
#endif

// LEDC PWM frequency in Hz for backlight dimming.
// Optimal value depends on the board's MOSFET circuit.
// Lower frequencies give wider dimming range but may cause audible coil whine.
#ifndef TFT_BACKLIGHT_PWM_FREQ
#define TFT_BACKLIGHT_PWM_FREQ 1000  // 1 kHz default (wide range, may whine on some boards)
#endif

// LEDC duty range for backlight dimming (8-bit: 0-255).
// Maps the visible dimming range to 1-99% brightness.
// Below DUTY_MIN the backlight is off; above DUTY_MAX it's fully saturated.
// 100% always uses duty 255 (constant DC, max brightness).

// Duty cycle where backlight first turns on.
#ifndef TFT_BACKLIGHT_DUTY_MIN
#define TFT_BACKLIGHT_DUTY_MIN 0
#endif
// Duty cycle at full saturation (before constant DC).
#ifndef TFT_BACKLIGHT_DUTY_MAX
#define TFT_BACKLIGHT_DUTY_MAX 255
#endif

// ============================================================================
// Touch Configuration
// ============================================================================
// Enable touch input support.
#ifndef HAS_TOUCH
#define HAS_TOUCH false
#endif

// Touch driver selection
// Available drivers:
//   TOUCH_DRIVER_XPT2046 (1) - XPT2046 resistive touch (via TFT_eSPI)
//   TOUCH_DRIVER_FT6236 (2) - FT6236 capacitive touch (future support)
//   TOUCH_DRIVER_AXS15231B_I2C (3) - AXS15231B capacitive touch (I2C, JC3248W535)
//   TOUCH_DRIVER_GT911 (5) - GT911 capacitive touch (I2C)
//   TOUCH_DRIVER_CST816S_WIRE (6) - CST816S capacitive touch (Wire I2C, JC3636W518)
#define TOUCH_DRIVER_XPT2046 1
#define TOUCH_DRIVER_FT6236 2
#define TOUCH_DRIVER_AXS15231B_I2C 3
#define TOUCH_DRIVER_GT911 5
#define TOUCH_DRIVER_CST816S_WIRE 6

// Touch reset pin (-1 = no hardware reset, GT911 boots normally).
#ifndef TOUCH_RST
#define TOUCH_RST -1
#endif

// I2C bus for touch controller: 0 = Wire, 1 = Wire1.
// Default: Wire1 to avoid ISR contention with WiFi on dual-core ESP32.
// ESP32-P4 can use Wire (bus 0) since WiFi runs on external C6 over SDIO.
#ifndef TOUCH_I2C_BUS
#define TOUCH_I2C_BUS 1
#endif

// Prefer allocating LVGL draw buffer in internal RAM before PSRAM.
// Default: false (keeps historical PSRAM-first behavior; boards can override).
// Prefer internal RAM over PSRAM for LVGL draw buffer allocation.
#ifndef LVGL_BUFFER_PREFER_INTERNAL
#define LVGL_BUFFER_PREFER_INTERNAL false
#endif

// ============================================================================
// Diagnostics / Telemetry
// ============================================================================
// Low-memory tripwire: when the internal heap minimum free (bytes) drops below this
// threshold, dump per-task stack watermarks once.
// Default: disabled (0). Enable per-board if you want early warning logs.
#ifndef MEMORY_TRIPWIRE_INTERNAL_MIN_BYTES
#define MEMORY_TRIPWIRE_INTERNAL_MIN_BYTES 0
#endif

// How often to check tripwires from the main loop.
#ifndef MEMORY_TRIPWIRE_CHECK_INTERVAL_MS
#define MEMORY_TRIPWIRE_CHECK_INTERVAL_MS 5000
#endif

// ============================================================================
// Web Portal
// ============================================================================
// Max JSON body size accepted by /api/config.
#ifndef WEB_PORTAL_CONFIG_MAX_JSON_BYTES
#define WEB_PORTAL_CONFIG_MAX_JSON_BYTES 4096
#endif

// Timeout for an incomplete /api/config upload (ms) before freeing the buffer.
#ifndef WEB_PORTAL_CONFIG_BODY_TIMEOUT_MS
#define WEB_PORTAL_CONFIG_BODY_TIMEOUT_MS 5000
#endif

// Select the touch HAL backend (one of the TOUCH_DRIVER_* constants).
#ifndef TOUCH_DRIVER
#define TOUCH_DRIVER TOUCH_DRIVER_XPT2046  // Default to XPT2046
#endif

#endif // BOARD_CONFIG_H

