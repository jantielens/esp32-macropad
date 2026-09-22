#pragma once

// Experimental Inkplate 6FLICK always-on LVGL mode. This target uses the
// 3MB no-OTA partition; the existing inkplate6flick board remains the
// duty-cycled grayscale dashboard.
#define HAS_DISPLAY true
#define HAS_TOUCH true
#define HAS_EPAPER false
#define HAS_AUDIO false
#define HAS_SOUND_PLAYER false
#define HAS_BLE_HID false
#define HAS_BLE false
#define HAS_IMAGE_FETCH false
#define HAS_IMAGE_LIBRARY false
#define HAS_CUSTOM_FONTS true
#define HAS_BUTTON false
#define HAS_BACKLIGHT false

// Keep decoded ARGB icon buffers modest on the classic ESP32's 8 MB PSRAM.
#define ICON_MAX_DIMENSION 128

#define HAS_MQTT true
// The classic ESP32 FreeRTOS port rejects the history worker's PSRAM static
// stack. Live MQTT bindings remain available without history backfill.
#define HAS_HA_HISTORY false
#define HAS_MCP false

#define DISPLAY_DRIVER DISPLAY_DRIVER_INKPLATE6FLICK
#define TOUCH_DRIVER TOUCH_DRIVER_INKPLATE6FLICK
#define DISPLAY_WIDTH 1024
#define DISPLAY_HEIGHT 758
#define DISPLAY_ROTATION 0

// E-paper has slow waveforms. The driver coalesces LVGL output, so this only
// limits rendering work; it does not delay touch polling.
#define LVGL_REFR_PERIOD_MS 100
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 8)
#define LVGL_BUFFER_PREFER_INTERNAL false
#define LVGL_TASK_CORE 1
// AsyncTCP's default 16 KB internal task stack is excessive for the portal;
// other portal boards use 8 KB with ample observed margin.
#define CONFIG_ASYNC_TCP_STACK_SIZE 8192
// Classic ESP32 FreeRTOS rejects PSRAM-backed static task stacks.
#define LVGL_TASK_USE_PSRAM_STACK false
// A white canvas minimizes full-panel black refreshes on grayscale e-paper.
#define LVGL_THEME_DARK_MODE false
// Intermediate visual states are not useful with full-panel grayscale waveforms.
#define DISPLAY_DISABLE_ANIMATIONS true

// Grayscale requires a full-panel waveform; partial updates are unsupported.
// Minimum interval between completed panel waveforms.
#define INKPLATE_MIN_REFRESH_MS 1500
// Briefly coalesce an action's final layout and binding updates before display().
#define INKPLATE_REFRESH_SETTLE_MS 150