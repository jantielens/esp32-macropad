#ifndef PAD_CONFIG_H
#define PAD_CONFIG_H

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
// MAX_PAD_BUTTONS, MAX_GRID_COLS, MAX_GRID_ROWS are overridable per board.
#ifndef MAX_PAD_BUTTONS
#define MAX_PAD_BUTTONS       64
#endif
#ifndef MAX_GRID_COLS
#define MAX_GRID_COLS          8
#endif
#ifndef MAX_GRID_ROWS
#define MAX_GRID_ROWS          8
#endif

#define CONFIG_LABEL_MAX_LEN          192
#define CONFIG_ICON_ID_MAX_LEN         32
#define CONFIG_SCREEN_ID_MAX_LEN       32
#define CONFIG_MQTT_TOPIC_MAX_LEN     128
#define CONFIG_MQTT_PAYLOAD_MAX_LEN   128
#define CONFIG_KEY_SEQ_MAX_LEN        256
#define CONFIG_BEEP_PATTERN_MAX_LEN   128
#define CONFIG_VOLUME_MODE_MAX_LEN     8
#define CONFIG_VALUE_MAX_LEN          16
#define CONFIG_TIMER_CMD_MAX_LEN      12
#define CONFIG_ACTION_TYPE_MAX_LEN     16
#define CONFIG_LAYOUT_NAME_MAX_LEN     16
#define CONFIG_JSON_PATH_MAX_LEN       48
#define CONFIG_FORMAT_MAX_LEN          24
#define CONFIG_STATE_ON_VALUE_MAX_LEN  32
#define CONFIG_BTN_STATE_MAX_LEN      192
#define CONFIG_CONFIRM_TEXT_MAX_LEN   128
#define CONFIG_BG_IMAGE_URL_MAX_LEN   256
#define CONFIG_BG_IMAGE_USER_MAX_LEN   32
#define CONFIG_BG_IMAGE_PASS_MAX_LEN   64
#define CONFIG_LABEL_STYLE_MAX_LEN    128
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
#define WIDGET_CONFIG_MAX_BYTES      1600

// Icon position relative to center label
#define ICON_POS_ABOVE   0   // Default: icon above center label
#define ICON_POS_LEFT    1   // Icon left of center label (row)
#define ICON_POS_CENTER  2   // Icon centered (no label displacement)

// ============================================================================
// Label Style — per-label visual overrides (parsed from DSL string)
// ============================================================================
// DSL format: "font:24;font_upscale:1.4;align:right;y:-3;mode:scroll;color:#FF0000"
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
    uint8_t font_size;     // 0 = auto (from scale tier), 12/14/18/24/32/36/48
    uint8_t font_family;   // 0 = default (Montserrat), 1 = dseg7, 2 = bebas, 3 = doto
    uint16_t font_upscale; // 0 = 1.0x (disabled), else LVGL transform scale (256 = 1.0x)
    int16_t x_offset;      // pixel nudge from default anchor (-999..+999)
    int16_t y_offset;      // pixel nudge from default anchor (-999..+999)
    uint8_t align;         // LABEL_ALIGN_* (0 = default/center)
    uint8_t long_mode;     // LABEL_MODE_* (0 = default/clip)
    uint32_t color;        // 0 = inherit fg_color, else 0x00RRGGBB with high byte = 1 to mark as set
};

// Parse a label style DSL string into a LabelStyle struct.
// Unknown keys are silently ignored. Empty/null input → all defaults (zeros).
// When the `color:` value is a binding template (contains '['), the raw token is
// copied to color_bind_out (if provided) and out->color is left unset (marker bit
// clear) so the runtime resolves it live; a static #RRGGBB sets out->color instead.
void label_style_parse(const char* dsl, LabelStyle* out,
                       char* color_bind_out = nullptr, size_t color_bind_len = 0);

// Action types (string constants for type field)
#define ACTION_TYPE_NONE     ""
#define ACTION_TYPE_SCREEN   "screen"
#define ACTION_TYPE_MQTT     "mqtt"
#define ACTION_TYPE_BACK     "back"
#define ACTION_TYPE_KEY      "key"
#define ACTION_TYPE_BLE_PAIR "ble_pair"
#define ACTION_TYPE_BEEP     "beep"
#define ACTION_TYPE_VOLUME     "volume"
#define ACTION_TYPE_BRIGHTNESS "brightness"
#define ACTION_TYPE_TIMER    "timer"
#define ACTION_TYPE_SOUND    "sound"
#define ACTION_TYPE_NOTIFY   "notify"
#define ACTION_TYPE_SYSTEM   "system"
#define ACTION_TYPE_HA_SERVICE "ha_service"
#define ACTION_TYPE_VISUAL_ALERT "visual_alert"

// Maximum number of sequential actions per tap or long-press
#define MAX_BUTTON_ACTIONS   3

// ============================================================================
// ButtonAction — typed tap / long-press action
// ============================================================================
// Storage is a discriminated union: ButtonAction::type names the active arm
// of ActionPayload. Only the active arm is valid; reading or writing a
// non-active arm is undefined behavior.
//
// All built-in action arms live here in common code so every device-class
// build pays the same per-action memory cost (the canary is
// tests/test_action_sizes.cpp). Device-class arms are added by conditionally
// including their header below.
//
// JSON wire format is intentionally *flat* (e.g. `{"type":"mqtt","topic":...,
// "payload":...}`) — see action_parse.cpp for the type-dispatched mapping
// between flat JSON keys and the active union arm.

struct ScreenPayload {
    char screen_id[CONFIG_SCREEN_ID_MAX_LEN];        // target screen
};
struct MqttPayload {
    char mqtt_topic[CONFIG_MQTT_TOPIC_MAX_LEN];      // publish topic
    char mqtt_payload[CONFIG_MQTT_PAYLOAD_MAX_LEN];  // publish payload
};
struct KeyPayload {
    char key_sequence[CONFIG_KEY_SEQ_MAX_LEN];       // DSL key sequence
};
struct BeepPayload {
    char beep_pattern[CONFIG_BEEP_PATTERN_MAX_LEN];  // "freq:dur freq:dur" (empty = default)
    uint8_t beep_volume;                             // 0 = device volume, 1-100 = override
};
struct VolumePayload {
    char volume_mode[CONFIG_VOLUME_MODE_MAX_LEN];    // "set" or "adjust"
    char volume_value[CONFIG_VALUE_MAX_LEN];         // absolute 0-100, signed delta, or {step}
};
struct BrightnessPayload {
    char brightness_mode[CONFIG_VOLUME_MODE_MAX_LEN]; // "set" or "adjust"
    char brightness_value[CONFIG_VALUE_MAX_LEN];      // absolute 5-100, signed delta, or {step}
};
struct TimerPayload {
    uint8_t timer_id;                                 // 1-3
    char timer_command[CONFIG_TIMER_CMD_MAX_LEN];     // "toggle", "start", "stop", "adjust", "set", etc.
    char timer_value[CONFIG_VALUE_MAX_LEN];           // seconds for set/adjust (supports {step})
};
struct SoundPayload {
    char sound_file[32];                              // filename (no path/extension)
    uint8_t sound_volume;                             // 0 = device vol, 1-100 = override
};
struct NotifyPayload {
    char notify_text[128];                                // message text (bindable). NOTE: dominates union size — see Future Work.
    char notify_duration_ms[CONFIG_BINDABLE_SHORT_LEN];   // auto-dismiss delay (bindable), "0" = persistent
    char notify_text_color[CONFIG_BINDABLE_SHORT_LEN];    // text color hex (bindable)
    char notify_bg_color[CONFIG_BINDABLE_SHORT_LEN];      // background color hex (bindable)
    char notify_border_color[CONFIG_BINDABLE_SHORT_LEN];  // border color hex (bindable)
    uint8_t notify_opacity;                               // 0-100%, 0 = use default (85)
    uint8_t notify_font_size;                             // 0 = auto
    char notify_location[8];                              // "top", "center", "bottom"
};
struct SystemPayload {
    char system_command[CONFIG_ACTION_TYPE_MAX_LEN];      // "reboot", "wifi_reconnect", "screensaver"
};
struct HaServicePayload {
    char entity_id[48];   // e.g. "light.living_room" (domain = text before first '.')
    char service[20];     // e.g. "toggle", "turn_on", "set_cover_position"
    char data_json[64];   // optional extra JSON object, e.g. {"brightness_pct":80}
};
struct VisualAlertPayload {
    char     va_op[8];                            // "start" (default) | "stop"
    char     va_color[CONFIG_BINDABLE_SHORT_LEN]; // overlay color hex (bindable), "" = red
    char     va_pattern[8];                       // "breathe" (default) | "blink" | "solid"
    uint16_t va_period_ms;                        // pulse cadence, 0 = default 800
    uint16_t va_intensity;                        // max overlay opacity 0-100%, 0 = default 100
    uint32_t va_duration_ms;                       // 0 = persist until stopped
};

// Opaque slot reserved for device-class action payloads. Each device class
// registers its own ActionTypeDef (via REGISTER_ACTION_TYPE) and casts the
// raw bytes to its payload struct at the parse/serialize/dispatch boundary —
// see device_classes/shutter_tester/shutter_payload.h for the reference
// pattern (struct + static_assert + inline accessor).
//
// Sized to fit today's largest device-class payload with headroom (today:
// ShutterPayload at 76 B = CONFIG_TIMER_CMD_MAX_LEN(12) + CONFIG_BINDABLE_SHORT_LEN(64)).
// Stays well under the dominant built-in arm (NotifyPayload at 394 B), so
// this slot does not currently move sizeof(ActionPayload). A board that
// needs more can raise this via board_overrides.h; never raise the default
// to fit a single class — that would push cost onto every board.
#ifndef ACTION_PAYLOAD_DEVICE_CLASS_BYTES
#define ACTION_PAYLOAD_DEVICE_CLASS_BYTES 96
#endif

union ActionPayload {
    ScreenPayload     screen;       // type == ACTION_TYPE_SCREEN
    MqttPayload       mqtt;         // type == ACTION_TYPE_MQTT
    KeyPayload        key;          // type == ACTION_TYPE_KEY
    BeepPayload       beep;         // type == ACTION_TYPE_BEEP
    VolumePayload     volume;       // type == ACTION_TYPE_VOLUME
    BrightnessPayload brightness;   // type == ACTION_TYPE_BRIGHTNESS
    TimerPayload      timer;        // type == ACTION_TYPE_TIMER
    SoundPayload      sound;        // type == ACTION_TYPE_SOUND
    NotifyPayload     notify;       // type == ACTION_TYPE_NOTIFY
    SystemPayload     system;       // type == ACTION_TYPE_SYSTEM
    HaServicePayload  ha_service;   // type == ACTION_TYPE_HA_SERVICE
    VisualAlertPayload visual_alert; // type == ACTION_TYPE_VISUAL_ALERT
    uint8_t           device_class[ACTION_PAYLOAD_DEVICE_CLASS_BYTES];
                                    // opaque; owned by a registered ActionTypeDef
    // back, ble_pair, "" (none) carry no payload data — only the type tag.
};

struct ButtonAction {
    char type[CONFIG_ACTION_TYPE_MAX_LEN];  // discriminator; "" = none
    ActionPayload payload;                  // valid arm selected by `type`
};

// Sentinel for host-native size-budget tests + tooling.
#define ACTION_PAYLOAD_PRESENT 1

// ButtonAction has a hard size budget of 420 bytes (notify arm = 394 B dominates).
// Trimming notify_text length is tracked as separate follow-up work.
static_assert(sizeof(ButtonAction) <= 420,
              "ButtonAction size budget exceeded (>420 bytes)");

// Per-arm size dump for size-canary tests. Lists built-in arms only;
// device-class payloads share the opaque ACTION_PAYLOAD_DEVICE_CLASS_BYTES
// slot and their static_asserts live in their own headers.
#define ACTION_PAYLOAD_DUMP_ARMS(printf_fn) do { \
    printf_fn("  ButtonAction      = %zu\n", sizeof(ButtonAction));      \
    printf_fn("  ActionPayload     = %zu\n", sizeof(ActionPayload));     \
    printf_fn("  ScreenPayload     = %zu\n", sizeof(ScreenPayload));     \
    printf_fn("  MqttPayload       = %zu\n", sizeof(MqttPayload));       \
    printf_fn("  KeyPayload        = %zu\n", sizeof(KeyPayload));        \
    printf_fn("  BeepPayload       = %zu\n", sizeof(BeepPayload));       \
    printf_fn("  VolumePayload     = %zu\n", sizeof(VolumePayload));     \
    printf_fn("  BrightnessPayload = %zu\n", sizeof(BrightnessPayload)); \
    printf_fn("  TimerPayload      = %zu\n", sizeof(TimerPayload));      \
    printf_fn("  SoundPayload      = %zu\n", sizeof(SoundPayload));      \
    printf_fn("  NotifyPayload     = %zu\n", sizeof(NotifyPayload));     \
    printf_fn("  SystemPayload     = %zu\n", sizeof(SystemPayload));     \
    printf_fn("  HaServicePayload  = %zu\n", sizeof(HaServicePayload));  \
    printf_fn("  VisualAlertPayload= %zu\n", sizeof(VisualAlertPayload)); \
} while (0)

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

    // Raw binding token from each label style's `color:` sub-field (empty when the
    // color is static or absent). Sized at CONFIG_LABEL_STYLE_MAX_LEN because a
    // color token can never exceed the style DSL it is a substring of. Registered
    // as a runtime color binding so per-label text colors update live.
    char label_top_color_bind[CONFIG_LABEL_STYLE_MAX_LEN];
    char label_center_color_bind[CONFIG_LABEL_STYLE_MAX_LEN];
    char label_bottom_color_bind[CONFIG_LABEL_STYLE_MAX_LEN];

    // Icon reference
    char icon_id[CONFIG_ICON_ID_MAX_LEN];
    uint8_t icon_scale_pct;             // 0 = auto (widget-aware), 1-250 = explicit scale %
    uint8_t icon_position;              // 0=above (default), 1=left, 2=center
    int16_t ui_offset_x;                // Optional visual nudge X in px (+right, -left)
    int16_t ui_offset_y;                // Optional visual nudge Y in px (+down, -up)

    // Visual styling — color fields are strings that may contain binding templates
    // e.g. "#FF0000" (static) or "[expr:[mqtt:t;.;%s]==\"ON\"?\"#00FF00\":\"#333333\"]" (dynamic)
    char bg_color[CONFIG_COLOR_MAX_LEN];          // default "#333333"
    char fg_color[CONFIG_COLOR_MAX_LEN];          // default "#FFFFFF"
    char border_color[CONFIG_COLOR_MAX_LEN];      // default "#000000"
    char border_width[CONFIG_BINDABLE_SHORT_LEN]; // default "0" — static or binding
    char corner_radius[CONFIG_BINDABLE_SHORT_LEN]; // default "8" — static or binding
    char content_pad[CONFIG_BINDABLE_SHORT_LEN];  // default "4" — plain px inset for labels/icon/widget (0-50)

    // Typed actions (up to MAX_BUTTON_ACTIONS sequential actions per gesture)
    ButtonAction actions[MAX_BUTTON_ACTIONS];      // tap actions (executed sequentially)
    uint8_t action_count;                          // number of tap actions (0-3)
    ButtonAction lp_actions[MAX_BUTTON_ACTIONS];   // long-press actions (executed sequentially)
    uint8_t lp_action_count;                       // number of long-press actions (0-3)
    bool confirm;                                  // require confirmation before either action list
    char confirm_text[CONFIG_CONFIRM_TEXT_MAX_LEN]; // optional confirmation prompt

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

// Size canary: per-label color-bind capture must stay sized at the label-style cap
// (a color token is a substring of the style DSL), NOT the larger color cap. This
// guards against a future edit silently re-oversizing ScreenButtonConfig ×3×buttons.
static_assert(sizeof(((ScreenButtonConfig*)nullptr)->label_center_color_bind)
                  == CONFIG_LABEL_STYLE_MAX_LEN,
              "label color-bind fields must be sized at CONFIG_LABEL_STYLE_MAX_LEN");

// Named binding: [pad:name] resolves to the stored template at runtime.
// Names must match [a-zA-Z][a-zA-Z0-9_]* (max PAD_BINDING_NAME_MAX_LEN chars).
struct PadBinding {
    char name[PAD_BINDING_NAME_MAX_LEN];      // e.g. "power", "solar_current"
    char value[CONFIG_LABEL_MAX_LEN];         // binding template, e.g. "[mqtt:solar/power;$.value]"
};

// Device-level button defaults — fields that cascade to all buttons on the device
// when the per-button JSON field is missing/null. Uses same field types as
// ScreenButtonConfig so the cascade is a simple string copy.
// A field is "set" when its string is non-empty.
struct ButtonDefaults {
    char bg_color[CONFIG_COLOR_MAX_LEN];          // e.g. "#1a1a2e"
    char fg_color[CONFIG_COLOR_MAX_LEN];          // e.g. "#e0e0ff"
    char border_color[CONFIG_COLOR_MAX_LEN];      // e.g. "#333366"
    char border_width[CONFIG_BINDABLE_SHORT_LEN]; // e.g. "1"
    char corner_radius[CONFIG_BINDABLE_SHORT_LEN]; // e.g. "16"
    char content_pad[CONFIG_BINDABLE_SHORT_LEN];  // e.g. "8" — plain px inset (0-50)
    char label_top_style[CONFIG_LABEL_STYLE_MAX_LEN];
    char label_center_style[CONFIG_LABEL_STYLE_MAX_LEN];
    char label_bottom_style[CONFIG_LABEL_STYLE_MAX_LEN];
    uint8_t icon_position;                         // ICON_POS_ABOVE (0) = default
};

// Per-pad config
struct PadConfig {
    char layout[CONFIG_LAYOUT_NAME_MAX_LEN]; // "grid" or curated layout name
    uint8_t cols;                            // 1-8 (grid mode only)
    uint8_t rows;                            // 1-8 (grid mode only)
    char wake_screen[CONFIG_SCREEN_ID_MAX_LEN]; // screen to navigate to on screensaver sleep (empty = stay)
    char bg_color[CONFIG_COLOR_MAX_LEN];         // pad background color (default "#000000")

    // Template pad: inherit buttons from another pad into empty grid positions.
    // -1 = none (default). 0..MAX_PADS-1 = source pad index.
    // Merge is read-only (template buttons are never written to this pad's JSON).
    int8_t template_pad;

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

// Read just the pad's optional friendly "name" label into `out` (empty when
// unset). Returns true when a non-empty name was found. Cheap filtered read.
bool pad_config_read_name(uint8_t page, char* out, size_t out_len);

// Resolve a pad reference that is either the canonical id ('pad_N') or a
// friendly name (case-insensitive match against existing pads' "name"). Returns
// the pad index, or -1 with a human-readable reason in `err` (unknown name, or
// ambiguous name listing the matching ids so the caller can disambiguate).
int pad_config_resolve_ref(const char* ref, char* err, size_t err_len);

// Rebuild all in-RAM pad config caches from flash. Call when a shared
// dependency (e.g. device-level button defaults) changes.
void pad_config_rebuild_all_caches();

// Generation counter — incremented on every save/delete. PadScreen uses this
// to detect config changes and rebuild tiles.
uint32_t pad_config_get_generation();

#ifdef __cplusplus
}
#endif

#endif // PAD_CONFIG_H
