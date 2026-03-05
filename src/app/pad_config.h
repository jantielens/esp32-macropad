#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Pad Config — per-page button configuration stored on LittleFS
// ============================================================================
// Each page (0–7) is stored as /config/pad_N.json on LittleFS.
// The REST API saves raw JSON to preserve all fields (including future ones).
// pad_config_load() parses only the fields needed for rendering.

#define MAX_PAD_PAGES          8
#define MAX_PAD_BUTTONS       64
#define MAX_GRID_COLS          8
#define MAX_GRID_ROWS          8

#define CONFIG_LABEL_MAX_LEN          192
#define CONFIG_ICON_ID_MAX_LEN         32
#define CONFIG_SCREEN_ID_MAX_LEN       16
#define CONFIG_MQTT_TOPIC_MAX_LEN     128
#define CONFIG_MQTT_PAYLOAD_MAX_LEN   128
#define CONFIG_ACTION_TYPE_MAX_LEN     16
#define CONFIG_LAYOUT_NAME_MAX_LEN     16
#define CONFIG_JSON_PATH_MAX_LEN       48
#define CONFIG_FORMAT_MAX_LEN          24
#define CONFIG_STATE_ON_VALUE_MAX_LEN  32
#define CONFIG_BG_IMAGE_URL_MAX_LEN   256
#define CONFIG_BG_IMAGE_USER_MAX_LEN   32
#define CONFIG_BG_IMAGE_PASS_MAX_LEN   64
#define CONFIG_WIDGET_TYPE_MAX_LEN     16
#define WIDGET_CONFIG_MAX_BYTES        64

// Action types (string constants for type field)
#define ACTION_TYPE_NONE    ""
#define ACTION_TYPE_SCREEN  "screen"
#define ACTION_TYPE_MQTT    "mqtt"
#define ACTION_TYPE_BACK    "back"

// Typed action for tap or long-press
struct ButtonAction {
    char type[CONFIG_ACTION_TYPE_MAX_LEN];           // "screen", "mqtt", or "" (none)
    char screen_id[CONFIG_SCREEN_ID_MAX_LEN];        // type="screen": target screen
    char mqtt_topic[CONFIG_MQTT_TOPIC_MAX_LEN];      // type="mqtt": publish topic
    char mqtt_payload[CONFIG_MQTT_PAYLOAD_MAX_LEN];  // type="mqtt": publish payload
};

// LabelBinding removed — MQTT bindings are now inline in label text.
// Use [mqtt:topic;path;format] syntax in label_top/center/bottom fields.

// Per-button toggle state binding (subscribe to topic, compare to on_value)
struct StateBinding {
    char mqtt_topic[CONFIG_MQTT_TOPIC_MAX_LEN];       // empty = no toggle
    char json_path[CONFIG_JSON_PATH_MAX_LEN];         // "." = raw value
    char on_value[CONFIG_STATE_ON_VALUE_MAX_LEN];     // value that means "ON"
};

// Widget type-specific config blob (parsed by widget implementations)
struct WidgetConfig {
    char type[CONFIG_WIDGET_TYPE_MAX_LEN];     // "" = normal button (default)
    char data_binding[CONFIG_LABEL_MAX_LEN];   // Binding template for widget data, e.g. [mqtt:topic;path]
    uint8_t data[WIDGET_CONFIG_MAX_BYTES];     // type-specific config, opaque to pad_config
};

// Per-button config (grid placement, labels, colors, typed actions)
struct ScreenButtonConfig {
    // Grid placement
    uint8_t col;
    uint8_t row;
    uint8_t col_span;   // default 1
    uint8_t row_span;   // default 1

    // Labels (any may be empty)
    char label_top[CONFIG_LABEL_MAX_LEN];
    char label_center[CONFIG_LABEL_MAX_LEN];
    char label_bottom[CONFIG_LABEL_MAX_LEN];

    // Icon reference
    char icon_id[CONFIG_ICON_ID_MAX_LEN];
    uint8_t icon_scale_pct;             // 0 = auto (widget-aware), 1-250 = explicit scale %

    // Visual styling
    uint32_t bg_color_rgb;          // default 0x333333
    uint32_t fg_color_rgb;          // default 0xFFFFFF
    uint32_t border_color_rgb;      // default 0x000000
    uint16_t border_width_px;       // default 0
    uint32_t disabled_fg_color_rgb; // default 0x444444
    uint16_t corner_radius_px;      // default 8

    // Typed actions
    ButtonAction action;     // tap action
    ButtonAction lp_action;  // long-press action

    // Toggle state binding (dim button when OFF)
    StateBinding state_bind;

    // Background image (fetched from URL, displayed as tile background)
    char bg_image_url[CONFIG_BG_IMAGE_URL_MAX_LEN];       // empty = no image
    char bg_image_user[CONFIG_BG_IMAGE_USER_MAX_LEN];     // HTTP Basic Auth user
    char bg_image_password[CONFIG_BG_IMAGE_PASS_MAX_LEN]; // HTTP Basic Auth password
    uint32_t bg_image_interval_ms;                        // 0 = fetch once, >0 = periodic
    bool bg_image_letterbox;                              // true = letterbox (fit + black bars), false = cover (fill + crop)

    // Widget type (bar_chart, gauge, etc.) — empty = normal button
    WidgetConfig widget;
};

// Page-level config
struct PadPageConfig {
    char layout[CONFIG_LAYOUT_NAME_MAX_LEN]; // "grid" or curated layout name
    uint8_t cols;                            // 1-8 (grid mode only)
    uint8_t rows;                            // 1-8 (grid mode only)
    char wake_screen[CONFIG_SCREEN_ID_MAX_LEN]; // screen to navigate to on screensaver sleep (empty = stay)
    uint8_t button_count;
    ScreenButtonConfig buttons[MAX_PAD_BUTTONS];
};

#ifdef __cplusplus
extern "C" {
#endif

// Mount LittleFS filesystem. Call once at boot. Returns true on success.
bool pad_config_init();

// Load page config from LittleFS JSON. Caller provides PadPageConfig buffer.
// On success, out is populated and returns true. On failure (file missing,
// parse error), out is zeroed and returns false.
bool pad_config_load(uint8_t page, PadPageConfig* out);

// Save raw JSON bytes to LittleFS. Preserves all fields including future/unknown ones.
bool pad_config_save_raw(uint8_t page, const uint8_t* json, size_t len);

// Delete page config file from LittleFS.
bool pad_config_delete(uint8_t page);

// Check if page config file exists on LittleFS.
bool pad_config_exists(uint8_t page);

// Read raw JSON from LittleFS. Caller must free() the returned buffer.
// Returns NULL on failure. *out_len is set to the file size.
char* pad_config_read_raw(uint8_t page, size_t* out_len);

// Generation counter — incremented on every save/delete. PadScreen uses this
// to detect config changes and rebuild tiles.
uint32_t pad_config_get_generation();

#ifdef __cplusplus
}
#endif
