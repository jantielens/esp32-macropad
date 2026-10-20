#pragma once

#ifndef HAS_REMOTE_LOG
#define HAS_REMOTE_LOG true
#endif

// ===========================================================================
// Board Overrides: reterminal-e1003-interactive (Seeed reTerminal E1003)
// ===========================================================================
// Always-on interactive profile for the 10.3" 1404x1872 IT8951 grayscale
// e-paper panel. The panel is portrait to the user and uses the regular LVGL
// macropad stack rather than the sleep-first E-Paper Frame lifecycle.

#define HAS_DISPLAY true
#define HAS_TOUCH true
#define HAS_EPAPER_PANEL true
#define HAS_LVGL_EPAPER true
#define HAS_NATIVE_EXTENSIONS true
#define HAS_AUDIO false
#define HAS_SOUND_PLAYER false
#define HAS_IMAGE_LIBRARY false
#define HAS_IMAGE_FETCH false

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

// Keep decoded ARGB icon buffers within the PSRAM headroom left by the
// e-paper framebuffers and LVGL allocations.
#define ICON_MAX_DIMENSION 192

// --- Driver Selection (HAL) ------------------------------------------------
#define DISPLAY_DRIVER DISPLAY_DRIVER_RETERMINAL_E1003
#define IT8951_E1003_PROTOCOL true
#define EPAPER_DEFAULT_VCOM_MV 1400
#define EPAPER_SPI_SCK_PIN 7
// E-paper SPI data-input GPIO for IT8951 responses.
#define EPAPER_SPI_MISO_PIN 8
#define EPAPER_SPI_MOSI_PIN 9
#define EPAPER_CS_PIN 10
#define EPAPER_BUSY_PIN 13
#define EPAPER_RESET_PIN 12
#define IT8951_PANEL_POWER_PIN 11
#define IT8951_CONTROLLER_POWER_PIN 21
// Panel IC name used by the generated board-to-driver table.
#define DISPLAY_PANEL "IT8951"
#define TOUCH_DRIVER TOUCH_DRIVER_GT911

#define DISPLAY_WIDTH 1872
#define DISPLAY_HEIGHT 1404
#define DISPLAY_ROTATION 1
// The E1003 panel is a rectangular portrait display.
#define DISPLAY_SHAPE DISPLAY_SHAPE_RECT

// The GT911, SHT4x, and RTC share these physical I2C lines. Wire1 maps to
// the same GPIOs while avoiding the WiFi ISR contention associated with Wire.
#define TOUCH_I2C_BUS 1
#define TOUCH_I2C_SDA 19
#define TOUCH_I2C_SCL 20
#define TOUCH_I2C_ADDR 0x5D
#define TOUCH_INT 2
#define TOUCH_RST 16

// E-paper physical updates are slow, so coalesce final LVGL states before
// presenting them. The persisted portal settings select B/W or grayscale on
// the next boot.
#define EPAPER_DEFAULT_RENDER_MODE EPAPER_RENDER_MODE_GRAYSCALE
#define EPAPER_MIN_REFRESH_INTERVAL_MS 500
#define EPAPER_REFRESH_SETTLE_MS 150
#define DISPLAY_BINDING_REFRESH_INTERVAL_MS 1000
#define DISPLAY_DISABLE_ANIMATIONS true
#define LVGL_REFR_PERIOD_MS 100
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 4)
#define LVGL_BUFFER_PREFER_INTERNAL false

// Serial is the CH340 bridge on UART0 (GPIO43/44), not native USB CDC.
#define CONFIG_DEFAULT_PORTAL_IDLE_SECONDS 300