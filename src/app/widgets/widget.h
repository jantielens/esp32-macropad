#ifndef WIDGETS_WIDGET_H
#define WIDGETS_WIDGET_H

#include "../board_config.h"

// Utility: clamp a value to [lo, hi].  Placed outside the HAS_DISPLAY guard
// so host-side tests can include it without pulling in LVGL dependencies.
template<typename T>
inline T clamp_val(T v, T lo, T hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

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
// 4. Add  REGISTER_WIDGET(name, stream_fn);  at the bottom (nullptr if no data stream)

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

// ----------------------------------------------------------------------------
// Widget auto-registration macro.
// Derives prefix_parse, prefix_create, prefix_update, prefix_destroy,
// prefix_tick from the given prefix.  stream_fn is passed verbatim
// (use nullptr when the widget has no data-stream support).
// ----------------------------------------------------------------------------
#define REGISTER_WIDGET(prefix, stream_fn, resolve_in_tick_flag)               \
    static const WidgetType prefix##_widget_type = {                           \
        #prefix, prefix##_parse, prefix##_create, prefix##_update,             \
        prefix##_destroy, prefix##_tick, stream_fn, resolve_in_tick_flag,      \
        nullptr                                                                \
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
    static const WidgetType prefix##_widget_type = {                           \
        #prefix, prefix##_parse, prefix##_create, prefix##_update,             \
        prefix##_destroy, prefix##_tick, stream_fn, resolve_in_tick_flag,      \
        prefix##_describe                                                      \
    };                                                                         \
    static struct prefix##AutoReg {                                            \
        prefix##AutoReg() { widget_register(&prefix##_widget_type); }          \
    } _##prefix##_auto_reg
#else
#define REGISTER_WIDGET_SCHEMA(prefix, stream_fn, resolve_in_tick_flag)        \
    REGISTER_WIDGET(prefix, stream_fn, resolve_in_tick_flag)
#endif

#endif // HAS_DISPLAY

#endif // WIDGETS_WIDGET_H
