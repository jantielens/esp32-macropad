#pragma once

#define HAS_DISPLAY true
#define HAS_TOUCH false
#define HAS_EPAPER_PANEL true
#define HAS_LVGL_EPAPER true
#define HAS_EPAPER_GRAYSCALE false
#define HAS_BUTTON true
#define HAS_AUDIO false
#define HAS_SOUND_PLAYER false
#define HAS_NATIVE_EXTENSIONS false
#define HAS_IMAGE_LIBRARY false
#define HAS_IMAGE_FETCH false
#define HAS_USB_HID false
#define HAS_BLE_HID false
#define HAS_REMOTE_LOG true

#define DISPLAY_DRIVER DISPLAY_DRIVER_CROWPANEL_42
#define DISPLAY_PANEL "SSD1683"
#define DISPLAY_WIDTH 400
#define DISPLAY_HEIGHT 300
#define DISPLAY_ROTATION 0
#define DISPLAY_SHAPE DISPLAY_SHAPE_RECT
#define EPAPER_DEFAULT_RENDER_MODE EPAPER_RENDER_MODE_BW
#define EPAPER_MIN_REFRESH_INTERVAL_MS 500
#define EPAPER_REFRESH_SETTLE_MS 150
#define DISPLAY_BINDING_REFRESH_INTERVAL_MS 1000
#define DISPLAY_DISABLE_ANIMATIONS true
#define LVGL_REFR_PERIOD_MS 100
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 10)
#define LVGL_BUFFER_PREFER_INTERNAL false

#define BUTTON_PIN 2
#define BUTTON_ACTIVE_LOW true
#define NUM_HW_BUTTONS 6
#define MAX_HW_BUTTONS 6
// Optional board-default hardware tap/hold actions in the configuration JSON format.
#define HW_BUTTON_DEFAULTS_JSON "{\"buttons\":[" \
        "{}, {}," \
        "{\"tap_actions\":[{\"type\":\"button_nav\",\"command\":\"previous\"}]}," \
        "{\"tap_actions\":[{\"type\":\"button_nav\",\"command\":\"next\"}]}," \
        "{\"tap_actions\":[{\"type\":\"button_nav\",\"command\":\"tap\"}]," \
            "\"hold_actions\":[{\"type\":\"button_nav\",\"command\":\"tap_hold\"}]}," \
        "{\"tap_actions\":[{\"type\":\"display_refresh\",\"mode\":\"full\"}]}]}"
#ifdef __cplusplus
static constexpr HwButtonDef HW_BUTTON_DEFS[NUM_HW_BUTTONS] = {
    {2, true, "Menu"},
    {1, true, "Back"},
    {6, true, "Rotary Up"},
    {4, true, "Rotary Down"},
    {5, true, "Rotary OK"},
    {0, true, "Boot"},
};
#endif

#define CONFIG_DEFAULT_PORTAL_IDLE_SECONDS 300