#pragma once

#define BOARD_HARDWARE_SOURCE "sticky_hardware.cpp"
#define BOARD_HARDWARE_HEADER "sticky_hardware.h"
#define BOARD_SENSOR_SOURCE "drivers/sticky_sensors.cpp"
#define BOARD_ACTION_SOURCE "sticky_actions.cpp"
#define BOARD_HARDWARE_LOOP sticky_hardware_loop
#define HAS_AUTO_SHUTDOWN true
// Reuse the board's checked hardware power-off request.
#define BOARD_AUTO_SHUTDOWN_REQUEST(token) sticky_request_shutdown(token)
// Reserve one extra registry entry for the board-local shutdown action.
#define ACTION_TYPE_REGISTRY_MAX_TYPES 28
// Optional power-latch hook called before the rest of application setup.
#define BOARD_EARLY_POWER_INIT sticky_early_power_init
// Board readiness prerequisite for the selected display driver.
#define BOARD_DISPLAY_PREPARE sticky_hardware_ready
// Board readiness prerequisite before touch reset and bus initialization.
#define BOARD_TOUCH_PREPARE sticky_hardware_ready

#define HAS_DISPLAY true
#ifndef HAS_DISPLAY_DIAGNOSTICS
#define HAS_DISPLAY_DIAGNOSTICS true
#endif
#define HAS_TOUCH true
#define HAS_EPAPER_PANEL true
#define HAS_LVGL_EPAPER true
#define HAS_EPAPER_GRAYSCALE true
#define HAS_BACKLIGHT false
#define HAS_REMOTE_LOG true
#define HAS_MQTT true
#define HAS_RTC true
#define RTC_DRIVER RTC_DRIVER_PCF8563
#define RTC_I2C_ADDR 0x51
#define HAS_MCP true
#define HAS_FILESYSTEM true
#define HAS_BLE true
#define HAS_BLE_HID false
// BLE_BACKEND_NIMBLE_ARDUINO; numeric for build.sh's library flag exporter.
#ifndef BLE_BACKEND
#define BLE_BACKEND 1
#endif
#define LVGL_FAST_MEM_IN_IRAM false
// Prefer PSRAM for medium-sized ordinary allocations alongside Wi-Fi and BLE.
#define MALLOC_PSRAM_THRESHOLD_BYTES 128
#define HAS_USB_HID false
#define HAS_NATIVE_EXTENSIONS false
#define HAS_IMAGE_FETCH false
#define HAS_IMAGE_LIBRARY false
#define HAS_AUDIO true
#define HAS_SOUND_PLAYER false
#define HAS_AUDIO_INPUT false
#define HAS_SD_CARD false
#define USE_SD_STORAGE false
#define HAS_SENSOR_BATTERY_ADC false
#define HAS_BATTERY_INFO true
#define AUDIO_OUTPUT_DRIVER AUDIO_OUTPUT_DRIVER_STICKY_PWM

// Driver Selection (HAL)
#define DISPLAY_DRIVER DISPLAY_DRIVER_STICKY_EPAPER
#define DISPLAY_PANEL "SSD1677/SSD2677"
#define DISPLAY_WIDTH 800
#define DISPLAY_HEIGHT 480
#define DISPLAY_ROTATION 0
#define DISPLAY_SHAPE DISPLAY_SHAPE_RECT
#define EPAPER_SPI_SCK_PIN 13
#define EPAPER_SPI_MISO_PIN 12
#define EPAPER_SPI_MOSI_PIN 14
#define EPAPER_CS_PIN 15
#define EPAPER_DC_PIN 16
#define EPAPER_RESET_PIN 17
#define EPAPER_BUSY_PIN 18
#define EPAPER_POWER_PIN 47
#define EPAPER_DEFAULT_RENDER_MODE EPAPER_RENDER_MODE_BW
#define EPAPER_MIN_REFRESH_INTERVAL_MS 500
#define EPAPER_REFRESH_SETTLE_MS 150
#define DISPLAY_BINDING_REFRESH_INTERVAL_MS 1000
#define DISPLAY_DISABLE_ANIMATIONS true
#define LVGL_REFR_PERIOD_MS 100
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 10)
#define LVGL_BUFFER_PREFER_INTERNAL false

// The GT911 owns Wire1 initialization; the board owns Wire0 initialization.
#define TOUCH_DRIVER TOUCH_DRIVER_GT911
#define TOUCH_I2C_BUS 1
#define TOUCH_I2C_SDA 3
#define TOUCH_I2C_SCL 2
#define TOUCH_I2C_ADDR 0x5D
#define TOUCH_INT 21
#define TOUCH_RST 41
#define TOUCH_GT911_CONFIG_ORIENTATION true
#define SENSOR_I2C_SDA 1
#define SENSOR_I2C_SCL 0
#define SENSOR_I2C_FREQUENCY 400000

#define HAS_BUTTON true
#define HAS_CONFIG_MODE_BUTTON true
#define BUTTON_PIN 5
#define BUTTON_ACTIVE_LOW true
#define NUM_HW_BUTTONS 3
#ifdef __cplusplus
static constexpr HwButtonDef HW_BUTTON_DEFS[NUM_HW_BUTTONS] = {
    {4, true, "AI/OK"},
    {5, true, "Up"},
    {6, true, "Down"},
};
#endif

#define LVGL_THEME_DARK_MODE false
#define DEFAULT_PAD_BG_COLOR "#FFFFFF"
#define DEFAULT_BUTTON_BG_COLOR "#FFFFFF"
#define DEFAULT_BUTTON_FG_COLOR "#000000"
#define DEFAULT_BUTTON_BORDER_WIDTH "1"
#define DEFAULT_BUTTON_CORNER_RADIUS "0"
#define DEFAULT_PIXEL_SHIFT_DISTANCE 0
#define DEFAULT_BUTTON_SHADOW_ENABLED false
#define DEFAULT_WIDGET_SERIES_COLOR "#000000"
#define DEFAULT_WIDGET_SERIES_COLOR_2 "#555555"
#define DEFAULT_WIDGET_SERIES_COLOR_3 "#888888"
#define DEFAULT_WIDGET_SERIES_COLOR_4 "#AAAAAA"
#define DEFAULT_WIDGET_TRACK_COLOR "#DDDDDD"
#define DEFAULT_WIDGET_INDICATOR_COLOR "#000000"
#define DEFAULT_WIDGET_TICK_COLOR "#555555"
#define DEFAULT_WIDGET_ZONE_COLOR "#888888"
#define DEFAULT_WIDGET_REFERENCE_COLOR "#555555"
#define DEFAULT_WIDGET_TABLE_BG_COLOR "#FFFFFF"
#define DEFAULT_WIDGET_TABLE_FG_COLOR "#000000"
#define DEFAULT_WIDGET_TABLE_HEADER_COLOR "#000000"
#define CONFIG_DEFAULT_PORTAL_IDLE_SECONDS 300
