#ifndef WIDGETS_WIDGET_H
#define WIDGETS_WIDGET_H

#include "../board_config.h"

// Utility: clamp a value to [lo, hi].  Placed outside the HAS_DISPLAY guard
// so host-side tests can include it without pulling in LVGL dependencies.
template<typename T>
inline T clamp_val(T v, T lo, T hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

constexpr unsigned widget_default_hex_digit(char digit) {
    return digit <= '9' ? digit - '0' : (digit >= 'a' ? digit - 'a' : digit - 'A') + 10;
}

constexpr unsigned widget_default_rgb(const char* color) {
    return (widget_default_hex_digit(color[1]) << 20)
         | (widget_default_hex_digit(color[2]) << 16)
         | (widget_default_hex_digit(color[3]) << 12)
         | (widget_default_hex_digit(color[4]) << 8)
         | (widget_default_hex_digit(color[5]) << 4)
         | widget_default_hex_digit(color[6]);
}

#if HAS_DISPLAY

#include "../binding_template.h"
#include "../pad_layout.h"
#include "widget_registry.h"
#include <ArduinoJson.h>
#include <lvgl.h>
#include <stdint.h>

// ============================================================================
// Widget Type Interface
// ============================================================================
// Each widget type (bar chart, gauge, etc.) implements this interface.
// PadScreen dispatches to the appropriate widget via widget_find().
//
// Adding a new widget type:
// 1. Create widgets/<name>_widget.cpp
// 2. Define a WidgetTypeConfig-sized config struct
// 3. Implement parse/create/update/destroy/tick functions
// 4. Define <name>_preview metadata, then REGISTER_WIDGET(name, stream_fn, resolve_in_tick).

// ---- JSON→string field parser for widget config ----
// Converts a JSON value (integer, hex string, or binding template) into a
// string stored in the widget config struct. For colors, integers are
// formatted as "#RRGGBB". For absent/null values, stores the default string.
inline void widget_parse_field(JsonVariant v, char* out, size_t out_len,
                               const char* def, bool is_color = true) {
    if (v.isNull() || (!v.is<const char*>() && !v.is<long>() && !v.is<unsigned long>())) {
        strlcpy(out, def, out_len);
        return;
    }
    if (v.is<long>() || v.is<unsigned long>()) {
        long val = v.as<long>();
        if (is_color) snprintf(out, out_len, "#%06lX", val & 0xFFFFFF);
        else          snprintf(out, out_len, "%ld", val);
        return;
    }
    const char* s = v.as<const char*>();
    strlcpy(out, (s && *s) ? s : def, out_len);
}

// ---- Runtime binding-aware resolvers ----
// Resolve a color string (static "#RRGGBB" or "[scheme:...]") to uint32_t RGB.
inline uint32_t resolve_color(const char* s, uint32_t def) {
    if (!s || !s[0]) return def;
#if HAS_MQTT
    if (binding_template_has_bindings(s)) {
        char resolved[BINDING_TEMPLATE_MAX_LEN];
        binding_template_resolve(s, resolved, sizeof(resolved));
        uint32_t out;
        return parse_hex_color(resolved, &out) ? out : def;
    }
#endif
    uint32_t out;
    return parse_hex_color(s, &out) ? out : def;
}

// Resolve a number string (static "8" or "[scheme:...]") to float.
inline float resolve_number(const char* s, float def) {
    if (!s || !s[0]) return def;
    if (binding_template_has_bindings(s)) {
        char resolved[BINDING_TEMPLATE_MAX_LEN];
        binding_template_resolve(s, resolved, sizeof(resolved));
        char* end = nullptr;
        float val = strtof(resolved, &end);
        return (end == resolved) ? def : val;
    }
    char* end = nullptr;
    float val = strtof(s, &end);
    return (end == s) ? def : val;
}

// Shorthand: resolve a color string directly to lv_color_t.
inline lv_color_t resolve_lv_color(const char* s, uint32_t def) {
    uint32_t rgb = resolve_color(s, def);
    return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// Cached variant: resolve color, skip LVGL setter if unchanged.
// `cache` must be initialized to UINT32_MAX (invalid sentinel) before first use.
// Returns true if the color changed (setter should be called).
#define COLOR_CACHE_INIT UINT32_MAX
inline bool resolve_color_changed(const char* s, uint32_t def, uint32_t* cache, lv_color_t* out) {
    uint32_t rgb = resolve_color(s, def);
    if (rgb == *cache) return false;
    *cache = rgb;
    *out = lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    return true;
}

#if HAS_MCP
inline void widget_describe_fill_fields(JsonArray fields, bool bar_patterns) {
    if (bar_patterns) {
        const char* patterns[] = {"widget_bar_pattern", "widget_bar_pattern_2", "widget_bar_pattern_3",
            "widget_bar_pattern_4", "widget_bar_bg_pattern"};
        const char* colors[] = {"widget_bar_pattern_color", "widget_bar_pattern_color_2", "widget_bar_pattern_color_3",
            "widget_bar_pattern_color_4", "widget_bar_bg_pattern_color"};
        for (unsigned index = 0; index < 5; ++index) {
            JsonObject pattern = fields.createNestedObject();
            pattern["name"] = patterns[index];
            pattern["type"] = "string";
            pattern["default"] = "none";
            JsonArray choices = pattern.createNestedArray("enum");
            for (const char* choice : {"none", "diagonal", "reverse_diagonal", "crosshatch", "dots", "checkerboard"}) choices.add(choice);
            JsonObject color = fields.createNestedObject();
            color["name"] = colors[index];
            color["type"] = "color";
            color["default"] = "#000000";
            color["max"] = index < 4 ? 191 : 63;
        }
    }
    const char* names[] = {"pattern_spacing", "pattern_stroke_width", "widget_track_outline_width", "widget_bar_track_radius"};
    const int minimum[] = {2, 1, 0, 0};
    const int maximum[] = {64, 8, 8, 64};
    const int defaults[] = {8, 1, 0, 4};
    for (unsigned index = 0; index < (bar_patterns ? 4u : 3u); ++index) {
        JsonObject field = fields.createNestedObject();
        field["name"] = names[index];
        field["type"] = "number";
        field["minimum"] = minimum[index];
        field["maximum"] = maximum[index];
        field["default"] = defaults[index];
    }
    JsonObject color = fields.createNestedObject();
    color["name"] = "widget_track_outline_color";
    color["type"] = "color";
    color["default"] = "#000000";
    color["max"] = 63;
}
#endif

// ----------------------------------------------------------------------------
// Widget auto-registration macro.
// Derives prefix_parse, prefix_create, prefix_update, prefix_destroy,
// prefix_tick from the given prefix.  stream_fn is passed verbatim
// (use nullptr when the widget has no data-stream support).
// ----------------------------------------------------------------------------
#define REGISTER_WIDGET(prefix, stream_fn, resolve_in_tick_flag)               \
    static_assert(sizeof(#prefix) <= CONFIG_WIDGET_TYPE_MAX_LEN,               \
                  "Widget identifier exceeds CONFIG_WIDGET_TYPE_MAX_LEN; shorten the prefix"); \
    static const WidgetType prefix##_widget_type = {                           \
        #prefix, prefix##_parse, prefix##_create, prefix##_update,             \
        prefix##_destroy, prefix##_tick, stream_fn, resolve_in_tick_flag,      \
        nullptr, nullptr, nullptr, nullptr, &prefix##_preview                  \
    };                                                                         \
    static struct prefix##AutoReg {                                            \
        prefix##AutoReg() { widget_register(&prefix##_widget_type); }          \
    } _##prefix##_auto_reg

// Variant that also wires a describeSchema hook (prefix##_describe).
// When MCP is compiled out there is no manifest consumer, so this collapses to
// plain REGISTER_WIDGET — the prefix##_describe function (and its string
// literals) must be guarded with #if HAS_MCP in the widget .cpp so nothing
// bleeds into non-MCP firmware.
#if HAS_MCP
#define REGISTER_WIDGET_SCHEMA(prefix, stream_fn, resolve_in_tick_flag)        \
    static_assert(sizeof(#prefix) <= CONFIG_WIDGET_TYPE_MAX_LEN,               \
                  "Widget identifier exceeds CONFIG_WIDGET_TYPE_MAX_LEN; shorten the prefix"); \
    static const WidgetType prefix##_widget_type = {                           \
        #prefix, prefix##_parse, prefix##_create, prefix##_update,             \
        prefix##_destroy, prefix##_tick, stream_fn, resolve_in_tick_flag,      \
        prefix##_describe, nullptr, nullptr, nullptr, &prefix##_preview        \
    };                                                                         \
    static struct prefix##AutoReg {                                            \
        prefix##AutoReg() { widget_register(&prefix##_widget_type); }          \
    } _##prefix##_auto_reg

#define REGISTER_WIDGET_SCHEMA_VALIDATED_LIFECYCLE(prefix, stream_fn, resolve_in_tick_flag, validate_fn) \
    static_assert(sizeof(#prefix) <= CONFIG_WIDGET_TYPE_MAX_LEN,               \
                  "Widget identifier exceeds CONFIG_WIDGET_TYPE_MAX_LEN; shorten the prefix"); \
    static const WidgetType prefix##_widget_type = {                           \
        #prefix, prefix##_parse, prefix##_create, prefix##_update,             \
        prefix##_destroy, prefix##_tick, stream_fn, resolve_in_tick_flag,      \
        prefix##_describe, prefix##_show, prefix##_hide, validate_fn, &prefix##_preview \
    };                                                                         \
    static struct prefix##AutoReg {                                            \
        prefix##AutoReg() { widget_register(&prefix##_widget_type); }          \
    } _##prefix##_auto_reg
#else
#define REGISTER_WIDGET_SCHEMA(prefix, stream_fn, resolve_in_tick_flag)        \
    REGISTER_WIDGET(prefix, stream_fn, resolve_in_tick_flag)

#define REGISTER_WIDGET_SCHEMA_VALIDATED_LIFECYCLE(prefix, stream_fn, resolve_in_tick_flag, validate_fn) \
    static_assert(sizeof(#prefix) <= CONFIG_WIDGET_TYPE_MAX_LEN,               \
                  "Widget identifier exceeds CONFIG_WIDGET_TYPE_MAX_LEN; shorten the prefix"); \
    static const WidgetType prefix##_widget_type = {                           \
        #prefix, prefix##_parse, prefix##_create, prefix##_update,             \
        prefix##_destroy, prefix##_tick, stream_fn, resolve_in_tick_flag,      \
        nullptr, prefix##_show, prefix##_hide, validate_fn, &prefix##_preview   \
    };                                                                         \
    static struct prefix##AutoReg {                                            \
        prefix##AutoReg() { widget_register(&prefix##_widget_type); }          \
    } _##prefix##_auto_reg
#endif

#define REGISTER_WIDGET_SCHEMA_LIFECYCLE(prefix, stream_fn, resolve_in_tick_flag) \
    REGISTER_WIDGET_SCHEMA_VALIDATED_LIFECYCLE(prefix, stream_fn, resolve_in_tick_flag, nullptr)

#endif // HAS_DISPLAY

#endif // WIDGETS_WIDGET_H
