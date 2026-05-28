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

// Enable MQTT and Home Assistant integration.
#ifndef HAS_MQTT
#define HAS_MQTT true
#endif

// Enable BLE HID keyboard support.
#ifndef HAS_BLE_HID
#define HAS_BLE_HID true
#endif

// Enable e-paper device class (Inkplate-style refresh-on-wake dashboards).
// When true the firmware compiles the e-paper HAL, the DutyCycleEpaper mode,
// Enable the e-paper refresh path and E-Paper portal page.
#ifndef HAS_EPAPER
#define HAS_EPAPER false
#endif

// Shutter-tester product variant. When true the firmware compiles the
// 3-or-4 sensor photodiode capture engine, shutter binding schemes, session
// storage, and portal pages — and the device identifies as the
// "Shutter Tester" device class (see device_class_registry). Off by default;
// enabled per-board via src/boards/<name>/board_overrides.h.
#ifndef IS_SHUTTER_TESTER
#define IS_SHUTTER_TESTER false
#endif

// Enable e-paper wake-button handling (ext0 wake plus short/long press).
#ifndef HAS_EPAPER_WAKE_BUTTON
#define HAS_EPAPER_WAKE_BUTTON false
#endif

// GPIO used for the e-paper wake button. Must be an RTC-capable input pin
// (typical Inkplate wiring uses GPIO36 with an external pullup).
#ifndef EPAPER_BUTTON_PIN
#define EPAPER_BUTTON_PIN 36
#endif

// Enable e-paper frontlight control on boards with frontlight hardware.
#ifndef HAS_EPAPER_FRONTLIGHT
#define HAS_EPAPER_FRONTLIGHT false
#endif

// E-paper full-refresh speed classification. When true, the boot path shows
// a short "Refreshing" splash on button wakes (OG inkplate-dashboard pattern).
// When false (default — most e-paper panels need 6-10 s per full refresh),
// button wakes skip straight to the image fetch to avoid the second waveform.
#ifndef EPAPER_FAST_REFRESH
#define EPAPER_FAST_REFRESH false
#endif

// Recovery / config portal first-boot auto-sleep default (seconds). Boards
// where the recovery portal is the primary user-facing surface (e.g. headless
// e-paper dashboards) can raise this so the user has more time to configure
// the device on a fresh flash.
#ifndef CONFIG_DEFAULT_PORTAL_IDLE_SECONDS
#define CONFIG_DEFAULT_PORTAL_IDLE_SECONDS 120
#endif

// Enable BTHome v2 BLE telemetry advertising (transport for headless/battery boards).
// Independent of HAS_BLE_HID; both can be enabled on the same board but mainly intended
// for boards without a display (e.g. ESP32-C3 sensor nodes).
#ifndef HAS_BLE
#define HAS_BLE false
#endif

// BLE telemetry advertising burst count (packets per wake in duty_cycle_ble mode).
#ifndef BLE_TELEMETRY_DEFAULT_BURST_COUNT
#define BLE_TELEMETRY_DEFAULT_BURST_COUNT 3
#endif

// BLE telemetry advertising interval (ms between adv packets within a burst).
#ifndef BLE_TELEMETRY_DEFAULT_ADV_INTERVAL_MS
#define BLE_TELEMETRY_DEFAULT_ADV_INTERVAL_MS 100
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

// WiFi reconnect tier thresholds (event-driven state machine).
// Tier 1: SDK auto-reconnect window — device takes no active reconnect action.
#ifndef WIFI_TIER1_DURATION_MS
#define WIFI_TIER1_DURATION_MS 60000
#endif

// Tier 2: active reconnect with exponential backoff.
#ifndef WIFI_TIER2_DURATION_MS
#define WIFI_TIER2_DURATION_MS 300000
#endif

// Total outage before controlled device reboot.
#ifndef WIFI_REBOOT_AFTER_MS
#define WIFI_REBOOT_AFTER_MS 600000
#endif

// Tier 2 exponential backoff: initial retry interval.
#ifndef WIFI_TIER2_BACKOFF_BASE_MS
#define WIFI_TIER2_BACKOFF_BASE_MS 10000
#endif

// Tier 2 exponential backoff: maximum retry interval cap.
#ifndef WIFI_TIER2_BACKOFF_MAX_MS
#define WIFI_TIER2_BACKOFF_MAX_MS 60000
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
// Audio (ES8311 codec + I2S, optional)
// ============================================================================
#ifndef HAS_AUDIO
#define HAS_AUDIO false
#endif

// NS4150B power amplifier enable pin.
#ifndef AUDIO_PA_PIN
#define AUDIO_PA_PIN -1
#endif

// PA enable polarity: false = active-HIGH (default), true = active-LOW.
// Some boards route the PA enable through an inverting transistor.
#ifndef AUDIO_PA_ACTIVE_LOW
#define AUDIO_PA_ACTIVE_LOW false
#endif

// I2C address of the audio codec (e.g. ES8311 = 0x18).
#ifndef AUDIO_CODEC_ADDR
#define AUDIO_CODEC_ADDR 0x18
#endif

// I2S master clock pin.
#ifndef AUDIO_I2S_MCLK
#define AUDIO_I2S_MCLK -1
#endif

// I2S bit clock pin.
#ifndef AUDIO_I2S_BCLK
#define AUDIO_I2S_BCLK -1
#endif

// I2S data out pin (ESP32 TX → codec data input).
#ifndef AUDIO_I2S_DOUT
#define AUDIO_I2S_DOUT -1
#endif

// I2S word select / left-right clock pin.
#ifndef AUDIO_I2S_LRCK
#define AUDIO_I2S_LRCK -1
#endif

// I2S data in pin (ESP32 RX ← codec data output).
#ifndef AUDIO_I2S_DIN
#define AUDIO_I2S_DIN -1
#endif

// Sound file player (MP3 playback from LittleFS).
// Defaults to HAS_AUDIO — enable audio to get sound player support.
#ifndef HAS_SOUND_PLAYER
#define HAS_SOUND_PLAYER HAS_AUDIO
#endif

// ============================================================================
// SD Card Storage (optional)
// ============================================================================
// Board has a physical MicroSD card slot wired to SDMMC.
#ifndef HAS_SD_CARD
#define HAS_SD_CARD false
#endif

// Route all persistent file I/O through the SD card instead of internal
// LittleFS. Implies HAS_SD_CARD. When true, the device halts at boot if
// the SD card is missing or unreadable — there is no runtime fallback.
// Eliminates display flicker on MIPI-DSI / RGB panels caused by internal
// flash cache-disable starving the framebuffer DMA.
#ifndef USE_SD_STORAGE
#define USE_SD_STORAGE false
#endif

// Run a diagnostic SD probe early in setup() (mount, card info, directory
// listing, write/read round-trip). Intended for new-board bring-up only.
#ifndef SD_PROBE_ON_BOOT
#define SD_PROBE_ON_BOOT false
#endif

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
// Shutter Tester (compile-time defaults — board overrides supply real pins)
// ============================================================================
// All shutter-tester runtime code is gated by IS_SHUTTER_TESTER; these defaults
// only matter when a shutter-tester board is built. Defaults are -1 (unwired)
// so non-shutter boards never accidentally drive a real GPIO.

// ADC input pin for shutter sensor 1.
#ifndef SHUTTER_ADC_PIN_S1
#define SHUTTER_ADC_PIN_S1 -1
#endif
// ADC input pin for shutter sensor 2.
#ifndef SHUTTER_ADC_PIN_S2
#define SHUTTER_ADC_PIN_S2 -1
#endif
// ADC input pin for shutter sensor 3.
#ifndef SHUTTER_ADC_PIN_S3
#define SHUTTER_ADC_PIN_S3 -1
#endif
// ADC input pin for shutter sensor 4 (used by 4-corner presets).
#ifndef SHUTTER_ADC_PIN_S4
#define SHUTTER_ADC_PIN_S4 -1
#endif
// ADC input pin for shutter sensor 5 — reserved for future presets.
#ifndef SHUTTER_ADC_PIN_S5
#define SHUTTER_ADC_PIN_S5 -1
#endif
// ADC input pin for shutter sensor 6 — reserved for future presets.
#ifndef SHUTTER_ADC_PIN_S6
#define SHUTTER_ADC_PIN_S6 -1
#endif
// ADC input pin for shutter sensor 7 — reserved for future presets.
#ifndef SHUTTER_ADC_PIN_S7
#define SHUTTER_ADC_PIN_S7 -1
#endif
// ADC input pin for shutter sensor 8 — reserved for future presets.
#ifndef SHUTTER_ADC_PIN_S8
#define SHUTTER_ADC_PIN_S8 -1
#endif
// ADC input pin for shutter sensor 9 — reserved for future presets.
#ifndef SHUTTER_ADC_PIN_S9
#define SHUTTER_ADC_PIN_S9 -1
#endif

// Maximum number of sensors across all capture presets (sized for the static
// position buffer). Runtime behavior is driven by the active preset.
#ifndef SHUTTER_SENSOR_MAX
#define SHUTTER_SENSOR_MAX 9
#endif

// Default preset id used when no stored config exists or the stored value is
// empty. Valid built-in values: "direct_single", "direct_3_line",
// "direct_4_corner". See shutter_capture.h for the preset registry.
#ifndef SHUTTER_DEFAULT_PRESET_ID
#define SHUTTER_DEFAULT_PRESET_ID "direct_3_line"
#endif

// Default horizontal sensor offset from centre sensor (S2) to outer sensor
// (S1/S3), in mm.
#ifndef SHUTTER_DEFAULT_OFFSET_X_MM
#define SHUTTER_DEFAULT_OFFSET_X_MM 11.2f
#endif
// Default vertical sensor offset from centre sensor (S2) to outer sensor
// (S1/S3), in mm.
#ifndef SHUTTER_DEFAULT_OFFSET_Y_MM
#define SHUTTER_DEFAULT_OFFSET_Y_MM 7.4f
#endif

// 35mm film diagonal for full-frame capping projection (sqrt(36² + 24²)).
#ifndef SHUTTER_FILM_DIAGONAL_MM
#define SHUTTER_FILM_DIAGONAL_MM 43.27f
#endif

// Pre-trigger sample count (kept here as a fallback; boards typically override).
#ifndef SHUTTER_PRE_TRIGGER_SAMPLES
#define SHUTTER_PRE_TRIGGER_SAMPLES 4096
#endif
// Post-pulse sample count (kept here as a fallback; boards typically override).
#ifndef SHUTTER_POST_CAPTURE_SAMPLES
#define SHUTTER_POST_CAPTURE_SAMPLES 4096
#endif

// Deviation verdict threshold (stops): values at or below this are PASS.
#ifndef SHUTTER_VERDICT_DEVIATION_WARNING
#define SHUTTER_VERDICT_DEVIATION_WARNING 0.333f
#endif
// Deviation verdict threshold (stops): values above this are a definite FAIL.
#ifndef SHUTTER_VERDICT_DEVIATION_FAIL
#define SHUTTER_VERDICT_DEVIATION_FAIL 0.500f
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
// timer).  Set to 0 on boards where these tasks interfere with real-time
// display rendering.  /api/health still works — it just returns
// point-in-time values without min/max window bands or CPU %.
#ifndef DEVICE_TELEMETRY_BACKGROUND_TASKS
#define DEVICE_TELEMETRY_BACKGROUND_TASKS 1
#endif

// Granular sub-flags — default to the master switch value so that
// DEVICE_TELEMETRY_BACKGROUND_TASKS=0 disables everything, but boards
// can selectively re-enable individual components.
// Enable CPU monitoring (idle-hook based, 1 Hz esp_timer).
#ifndef DEVICE_TELEMETRY_CPU_MONITOR
#define DEVICE_TELEMETRY_CPU_MONITOR DEVICE_TELEMETRY_BACKGROUND_TASKS
#endif
// Enable health-window min/max sampling timer.
#ifndef DEVICE_TELEMETRY_HEALTH_WINDOW
#define DEVICE_TELEMETRY_HEALTH_WINDOW DEVICE_TELEMETRY_BACKGROUND_TASKS
#endif
// Health window sampling period (ms).  Lower = finer min/max tracking but
// more frequent sampling.  Boards with PSRAM-backed framebuffers may need a
// higher value to avoid DMA bus contention.
#ifndef HEALTH_WINDOW_SAMPLE_PERIOD_MS
#define HEALTH_WINDOW_SAMPLE_PERIOD_MS 200
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

// Blank backlight during pad save to hide PSRAM bus contention flicker on MIPI-DSI panels.
// Continuous DMA scan can produce cyan/blue flashes during heavy PSRAM I/O
// (LittleFS + lodepng). The browser blanks/restores via /api/display/brightness.
#ifndef DISPLAY_BLANK_ON_SAVE
#define DISPLAY_BLANK_ON_SAVE false
#endif

// Enable custom display fonts (DSEG7, Bebas Neue, Doto).
// Requires HAS_DISPLAY. Generated by tools/generate-fonts.sh.
#ifndef HAS_CUSTOM_FONTS
#define HAS_CUSTOM_FONTS HAS_DISPLAY
#endif

// Enable background image fetch for pad button tiles.
// Requires HAS_DISPLAY. Uses LVGL's built-in tjpgd (JPEG) and lodepng (PNG).
#ifndef HAS_IMAGE_FETCH
#define HAS_IMAGE_FETCH HAS_DISPLAY
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
//   DISPLAY_DRIVER_JD9165_DSI (10) - Direct ESP-IDF JD9165 MIPI-DSI (JC1060P470C, ESP32-P4)
#define DISPLAY_DRIVER_TFT_ESPI 1
#define DISPLAY_DRIVER_LOVYANGFX 3
#define DISPLAY_DRIVER_ARDUINO_GFX 4
#define DISPLAY_DRIVER_ST7701_RGB 6
#define DISPLAY_DRIVER_ARDUINO_GFX_ST77916 7
#define DISPLAY_DRIVER_ST7703_DSI 8
#define DISPLAY_DRIVER_ST7701_DSI 9
#define DISPLAY_DRIVER_JD9165_DSI 10

// Select the display HAL backend (one of the DISPLAY_DRIVER_* constants).
#ifndef DISPLAY_DRIVER
#define DISPLAY_DRIVER DISPLAY_DRIVER_TFT_ESPI  // Default to TFT_eSPI
#endif

// Display shape constants (used by pad layout engine for grid/curated decisions)
#define DISPLAY_SHAPE_RECT   0  // Rectangular (landscape or portrait)
#define DISPLAY_SHAPE_SQUARE 1  // Square
#define DISPLAY_SHAPE_ROUND  2  // Circular panel

// Default display shape (boards override in board_overrides.h)
#ifndef DISPLAY_SHAPE
#define DISPLAY_SHAPE DISPLAY_SHAPE_RECT
#endif

// UI scale tier constants (selects font sizes and spacing for pad layout)
#define UI_SCALE_SMALL   0  // 320px-class displays
#define UI_SCALE_MEDIUM  1  // 360-480px displays
#define UI_SCALE_LARGE   2  // 480-720px displays
#define UI_SCALE_XLARGE  3  // 720px+ displays

// Default UI scale tier (boards override in board_overrides.h)
#ifndef UI_SCALE_TIER
#define UI_SCALE_TIER UI_SCALE_MEDIUM
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

// DSI timing defaults for JD9165 MIPI-DSI panels (ESP32-P4, direct ESP-IDF).
// Values from JC1060P470C BSP (HKC 7.0" IPS 1024x600), validated with vendor dtsi.
// DPI pixel clock in Hz.
#ifndef JD9165_DSI_DPI_CLK_HZ
#define JD9165_DSI_DPI_CLK_HZ 51200000L
#endif
// MIPI-DSI lane bit rate in Mbps.
#ifndef JD9165_DSI_LANE_BIT_RATE
#define JD9165_DSI_LANE_BIT_RATE 550
#endif
// HSYNC pulse width in pixel clocks.
#ifndef JD9165_DSI_HSYNC_PULSE_WIDTH
#define JD9165_DSI_HSYNC_PULSE_WIDTH 24
#endif
// HSYNC back porch in pixel clocks.
#ifndef JD9165_DSI_HSYNC_BACK_PORCH
#define JD9165_DSI_HSYNC_BACK_PORCH 136
#endif
// HSYNC front porch in pixel clocks.
#ifndef JD9165_DSI_HSYNC_FRONT_PORCH
#define JD9165_DSI_HSYNC_FRONT_PORCH 160
#endif
// VSYNC pulse width in lines.
#ifndef JD9165_DSI_VSYNC_PULSE_WIDTH
#define JD9165_DSI_VSYNC_PULSE_WIDTH 2
#endif
// VSYNC back porch in lines.
#ifndef JD9165_DSI_VSYNC_BACK_PORCH
#define JD9165_DSI_VSYNC_BACK_PORCH 21
#endif
// VSYNC front porch in lines.
#ifndef JD9165_DSI_VSYNC_FRONT_PORCH
#define JD9165_DSI_VSYNC_FRONT_PORCH 12
#endif

// ============================================================================
// LVGL Configuration
// ============================================================================
// LVGL draw buffer size in pixels (larger = faster, more RAM).
#ifndef LVGL_BUFFER_SIZE
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 10)  // 10 lines buffer
#endif

// Number of LVGL draw buffers (1 = single, 2 = double-buffered).
// Double-buffering lets LVGL render the next frame while the previous one
// is being flushed (useful with async DMA2D or PPA rotation pipelines).
#ifndef LVGL_DRAW_BUF_COUNT
#define LVGL_DRAW_BUF_COUNT 1
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

// Minimum brightness (%) a user can set via buttons, web UI, or API.
// The screen saver bypasses this floor to allow sleep (brightness 0).
#ifndef MIN_USER_BRIGHTNESS
#define MIN_USER_BRIGHTNESS 5
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
// Web Portal
// ============================================================================
// Max JSON body size accepted by /api/config.
#ifndef WEB_PORTAL_CONFIG_MAX_JSON_BYTES
#define WEB_PORTAL_CONFIG_MAX_JSON_BYTES 4096
#endif

// Default startup fragment for board variants with a primary portal category.
#ifndef PORTAL_PRIMARY_FRAGMENT
#define PORTAL_PRIMARY_FRAGMENT ""
#endif

// Custom nav category ID promoted to first position (empty = standard behavior).
#ifndef PORTAL_PRIMARY_CATEGORY
#define PORTAL_PRIMARY_CATEGORY ""
#endif

// Display name for the primary portal category in the nav sidebar.
#ifndef PORTAL_PRIMARY_LABEL
#define PORTAL_PRIMARY_LABEL ""
#endif

// Icon (UTF-8) for the primary portal category in the nav sidebar.
#ifndef PORTAL_PRIMARY_ICON
#define PORTAL_PRIMARY_ICON ""
#endif

// Timeout for an incomplete /api/config upload (ms) before freeing the buffer.
#ifndef WEB_PORTAL_CONFIG_BODY_TIMEOUT_MS
#define WEB_PORTAL_CONFIG_BODY_TIMEOUT_MS 5000
#endif

// Select the touch HAL backend (one of the TOUCH_DRIVER_* constants).
#ifndef TOUCH_DRIVER
#define TOUCH_DRIVER TOUCH_DRIVER_XPT2046  // Default to XPT2046
#endif

// ============================================================================
// Screensaver
// ============================================================================
// LVGL task loop delay while the screensaver is fully asleep (ms).
// Higher values save more CPU but increase wake latency (default 200 ms ≈ 5 Hz).
#ifndef SCREENSAVER_SLEEP_TICK_MS
#define SCREENSAVER_SLEEP_TICK_MS 200
#endif

// Periodic interval (ms) at which the screensaver calls
// DisplayDriver::displayRefreshSleep() while the display is fully asleep,
// for image-retention / VCOM-drift mitigation on cheap IPS panels.
// Default 15 minutes; 0 disables.
// Interval in ms between periodic asleep-display refresh calls (0 = disabled).
#ifndef SCREENSAVER_SLEEP_REFRESH_MS
#define SCREENSAVER_SLEEP_REFRESH_MS 900000
#endif

// Hold the panel hardware reset pin LOW during screensaver sleep so the
// panel IC fully powers down its internal regulators. Required on cheap
// IPS MIPI-DSI panels (e.g. JD9165 on jc1060p470c) where DCS Sleep In
// alone does not de-bias the TFT cells, leading to washed-out colors
// after multi-hour idle. Wake re-runs the full vendor init sequence,
// growing wake latency from ~120 ms to ~250-300 ms.
// Hold panel RST low during screensaver sleep (MipiDsiDriver only; needs LCD_RST_PIN).
#ifndef DISPLAY_HARD_RESET_ON_SLEEP
#define DISPLAY_HARD_RESET_ON_SLEEP false
#endif

// ============================================================================
// Pad & Screen Limits
// ============================================================================
// Maximum number of user-configurable pads the device supports.
// Override per-board in board_overrides.h for memory-constrained targets.
#ifndef MAX_PADS
#define MAX_PADS 16
#endif

// Number of non-pad screens (info, test, fps, touch_test, + headroom).
#ifndef MAX_NON_PAD_SCREENS
#define MAX_NON_PAD_SCREENS 10
#endif

// Total screen registry slots (derived from pad count + non-pad screens).
#define MAX_SCREENS (MAX_PADS + MAX_NON_PAD_SCREENS)

// Screen history depth for back-navigation. Also controls the LRU pad cache size.
#ifndef SCREEN_HISTORY_MAX
#define SCREEN_HISTORY_MAX 8
#endif

// Maximum number of concurrent data streams (ring buffers for sparkline widgets).
// Each stream uses ~220 bytes static + ~240 bytes PSRAM ring buffer when active.
#ifndef DATA_STREAM_MAX_STREAMS
#define DATA_STREAM_MAX_STREAMS 64
#endif

#endif // BOARD_CONFIG_H

