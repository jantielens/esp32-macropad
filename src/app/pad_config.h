#pragma once

#include "board_config.h"

#include <cstdlib>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Pad Config — per-pad button configuration stored on LittleFS
// ============================================================================
// Each pad (0..MAX_PADS-1) is stored as /config/pad_N.json on LittleFS.
// The REST API saves raw JSON to preserve all fields (including future ones).
// pad_config_load() parses only the fields needed for rendering.

// MAX_PADS is defined in board_config.h (overridable per board, default 16)
#define MAX_PAD_BUTTONS       64
#define MAX_GRID_COLS          8
#define MAX_GRID_ROWS          8

#define CONFIG_LABEL_MAX_LEN          192
#define CONFIG_ICON_ID_MAX_LEN         32
#define CONFIG_SCREEN_ID_MAX_LEN       16
#define CONFIG_MQTT_TOPIC_MAX_LEN     128
#define CONFIG_MQTT_PAYLOAD_MAX_LEN   128
#define CONFIG_KEY_SEQ_MAX_LEN        256
#define CONFIG_BEEP_PATTERN_MAX_LEN   128
#define CONFIG_VOLUME_MODE_MAX_LEN     8
#define CONFIG_ACTION_TYPE_MAX_LEN     16
#define CONFIG_LAYOUT_NAME_MAX_LEN     16
#define CONFIG_JSON_PATH_MAX_LEN       48
#define CONFIG_FORMAT_MAX_LEN          24
#define CONFIG_STATE_ON_VALUE_MAX_LEN  32
#define CONFIG_BTN_STATE_MAX_LEN      192
#define CONFIG_BG_IMAGE_URL_MAX_LEN   256
#define CONFIG_BG_IMAGE_USER_MAX_LEN   32
#define CONFIG_BG_IMAGE_PASS_MAX_LEN   64
#define CONFIG_LABEL_STYLE_MAX_LEN     64
#define PAD_MAX_BINDINGS              16
#define PAD_BINDING_NAME_MAX_LEN      32

// Parse hex color string (#RRGGBB, RRGGBB, 0xRRGGBB) to uint32_t.
// Returns false if unparseable (e.g. "---", "ERR:...", empty).
static inline bool parse_hex_color(const char* s, uint32_t* out) {
    if (!s || !s[0]) return false;
    if (s[0] == '#') s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    // Quick-reject non-hex leading chars (e.g. "ERR:...", "---")
    char c = s[0];
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
        return false;
    char* end = nullptr;
    unsigned long val = strtoul(s, &end, 16);
    if (end == s) return false;
    *out = (uint32_t)(val & 0xFFFFFF);
    return true;
}
#define CONFIG_COLOR_MAX_LEN            192
#define CONFIG_BINDABLE_SHORT_LEN       64
#define CONFIG_WIDGET_TYPE_MAX_LEN     16
#define MAX_WIDGET_BINDINGS             4
#define WIDGET_CONFIG_MAX_BYTES      1536

// ============================================================================
// Label Style — per-label visual overrides (parsed from DSL string)
// ============================================================================
// DSL format: "font:24;align:right;y:-3;mode:scroll;color:#FF0000"
// All fields default to 0 which means "use default behavior".

// Text alignment values
#define LABEL_ALIGN_DEFAULT  0  // center (default)
#define LABEL_ALIGN_LEFT     1
#define LABEL_ALIGN_RIGHT    2
#define LABEL_ALIGN_CENTER   3  // explicit center

// Long mode values
#define LABEL_MODE_DEFAULT   0  // clip (default)
#define LABEL_MODE_CLIP      1
#define LABEL_MODE_SCROLL    2
#define LABEL_MODE_DOT       3
#define LABEL_MODE_WRAP      4

struct LabelStyle {
    uint8_t font_size;     // 0 = auto (from scale tier), 12/14/18/24/32/36
    int16_t x_offset;      // pixel nudge from default anchor (-999..+999)
    int16_t y_offset;      // pixel nudge from default anchor (-999..+999)
    uint8_t align;         // LABEL_ALIGN_* (0 = default/center)
    uint8_t long_mode;     // LABEL_MODE_* (0 = default/clip)
    uint32_t color;        // 0 = inherit fg_color, else 0x00RRGGBB with high byte = 1 to mark as set
};

// Parse a label style DSL string into a LabelStyle struct.
// Unknown keys are silently ignored. Empty/null input → all defaults (zeros).
void label_style_parse(const char* dsl, LabelStyle* out);

// Action types (string constants for type field)
#define ACTION_TYPE_NONE     ""
#define ACTION_TYPE_SCREEN   "screen"
#define ACTION_TYPE_MQTT     "mqtt"
#define ACTION_TYPE_BACK     "back"
#define ACTION_TYPE_KEY      "key"
#define ACTION_TYPE_BLE_PAIR "ble_pair"
#define ACTION_TYPE_BEEP     "beep"
#define ACTION_TYPE_VOLUME   "volume"
#define ACTION_TYPE_TIMER    "timer"

// Typed action for tap or long-press
struct ButtonAction {
    char type[CONFIG_ACTION_TYPE_MAX_LEN];           // "screen", "mqtt", "key", "ble_pair", "beep", "volume", or "" (none)
    char screen_id[CONFIG_SCREEN_ID_MAX_LEN];        // type="screen": target screen
    char mqtt_topic[CONFIG_MQTT_TOPIC_MAX_LEN];      // type="mqtt": publish topic
    char mqtt_payload[CONFIG_MQTT_PAYLOAD_MAX_LEN];  // type="mqtt": publish payload
    char key_sequence[CONFIG_KEY_SEQ_MAX_LEN];       // type="key": DSL key sequence
    char beep_pattern[CONFIG_BEEP_PATTERN_MAX_LEN];  // type="beep": "freq:dur freq:dur" (empty = default)
    uint8_t beep_volume;                             // type="beep": 0 = use device volume, 1-100 = override
    char volume_mode[CONFIG_VOLUME_MODE_MAX_LEN];    // type="volume": "set", "up", or "down"
    uint8_t volume_value;                            // type="volume": 0-100 (used with mode="set")
    uint32_t timer_countdown;                        // type="timer": default countdown in seconds (0 = none)
    char timer_expire_beep[CONFIG_BEEP_PATTERN_MAX_LEN]; // type="timer": beep pattern on countdown expiry
    uint8_t timer_expire_volume;                      // type="timer": 0 = device vol, 1-100 = override
};

// LabelBinding removed — MQTT bindings are now inline in label text.
// Use [mqtt:topic;path;format] syntax in label_top/center/bottom fields.

// Widget type-specific config blob (parsed by widget implementations)
struct WidgetConfig {
    char type[CONFIG_WIDGET_TYPE_MAX_LEN];     // "" = normal button (default)
    char data_binding[MAX_WIDGET_BINDINGS][CONFIG_LABEL_MAX_LEN]; // Binding templates (0=primary, 1-3=extra)
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

    // Per-label style overrides (parsed from DSL strings)
    LabelStyle style_top;
    LabelStyle style_center;
    LabelStyle style_bottom;

    // Icon reference
    char icon_id[CONFIG_ICON_ID_MAX_LEN];
    uint8_t icon_scale_pct;             // 0 = auto (widget-aware), 1-250 = explicit scale %
    int16_t ui_offset_x;                // Optional visual nudge X in px (+right, -left)
    int16_t ui_offset_y;                // Optional visual nudge Y in px (+down, -up)

    // Visual styling — color fields are strings that may contain binding templates
    // e.g. "#FF0000" (static) or "[expr:[mqtt:t;.;%s]==\"ON\"?\"#00FF00\":\"#333333\"]" (dynamic)
    char bg_color[CONFIG_COLOR_MAX_LEN];          // default "#333333"
    char fg_color[CONFIG_COLOR_MAX_LEN];          // default "#FFFFFF"
    char border_color[CONFIG_COLOR_MAX_LEN];      // default "#000000"
    char border_width[CONFIG_BINDABLE_SHORT_LEN]; // default "0" — static or binding
    char corner_radius[CONFIG_BINDABLE_SHORT_LEN]; // default "8" — static or binding

    // Typed actions
    ButtonAction action;     // tap action
    ButtonAction lp_action;  // long-press action

    // Audio feedback overrides (empty = use device default, "none" = suppress)
    char tap_beep[CONFIG_BEEP_PATTERN_MAX_LEN];
    char lp_beep[CONFIG_BEEP_PATTERN_MAX_LEN];

    // Background image (fetched from URL, displayed as tile background)
    char bg_image_url[CONFIG_BG_IMAGE_URL_MAX_LEN];       // empty = no image
    char bg_image_user[CONFIG_BG_IMAGE_USER_MAX_LEN];     // HTTP Basic Auth user
    char bg_image_password[CONFIG_BG_IMAGE_PASS_MAX_LEN]; // HTTP Basic Auth password
    uint32_t bg_image_interval_ms;                        // 0 = fetch once, >0 = periodic
    bool bg_image_letterbox;                              // true = letterbox (fit + black bars), false = cover (fill + crop)

    // Widget type (bar_chart, gauge, etc.) — empty = normal button
    WidgetConfig widget;

    // Button state — tri-state: "enabled" (default), "disabled", "hidden"
    // Empty = enabled. Supports binding templates for dynamic state.
    char btn_state[CONFIG_BTN_STATE_MAX_LEN];
};

// Named binding: [pad:name] resolves to the stored template at runtime.
// Names must match [a-zA-Z][a-zA-Z0-9_]* (max PAD_BINDING_NAME_MAX_LEN chars).
struct PadBinding {
    char name[PAD_BINDING_NAME_MAX_LEN];      // e.g. "power", "solar_current"
    char value[CONFIG_LABEL_MAX_LEN];         // binding template, e.g. "[mqtt:solar/power;$.value]"
};

// Per-pad config
struct PadConfig {
    char layout[CONFIG_LAYOUT_NAME_MAX_LEN]; // "grid" or curated layout name
    uint8_t cols;                            // 1-8 (grid mode only)
    uint8_t rows;                            // 1-8 (grid mode only)
    char wake_screen[CONFIG_SCREEN_ID_MAX_LEN]; // screen to navigate to on screensaver sleep (empty = stay)
    char bg_color[CONFIG_COLOR_MAX_LEN];         // pad background color (default "#000000")

    // Named pad-level bindings for [pad:name] references
    uint8_t binding_count;
    PadBinding bindings[PAD_MAX_BINDINGS];

    uint8_t button_count;
    ScreenButtonConfig buttons[MAX_PAD_BUTTONS];
};

#ifdef __cplusplus
extern "C" {
#endif

// Mount LittleFS filesystem. Call once at boot. Returns true on success.
bool pad_config_init();

// Load pad config from LittleFS JSON. Caller provides PadConfig buffer.
// On success, out is populated and returns true. On failure (file missing,
// parse error), out is zeroed and returns false.
bool pad_config_load(uint8_t page, PadConfig* out);

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
