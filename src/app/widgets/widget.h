#pragma once

#include "../board_config.h"

// Utility: clamp a value to [lo, hi].  Placed outside the HAS_DISPLAY guard
// so host-side tests can include it without pulling in LVGL dependencies.
template<typename T>
inline T clamp_val(T v, T lo, T hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

#if HAS_DISPLAY

#include "../binding_template.h"
#include "../pad_config.h"
#include "../pad_layout.h"
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

// WidgetConfig is defined in pad_config.h (type[16] + data[WIDGET_CONFIG_MAX_BYTES])

// Opaque per-tile widget runtime state (LVGL objects, cached values, etc.)
// Created by createUI(), freed by destroyUI(). Stored in ButtonTile.
#define WIDGET_STATE_MAX_BYTES 80

struct WidgetState {
    uint8_t data[WIDGET_STATE_MAX_BYTES];
};

// Widget type vtable — each widget type provides one static instance.
struct WidgetType {
    const char* name;

    // Parse type-specific JSON fields into the config data blob.
    // Called during pad_config_load().
    void (*parseConfig)(const JsonObject& btn, uint8_t* data);

    // Create LVGL objects inside the tile container.
    // `rect` provides tile dimensions. `state` is zeroed on entry.
    // `icon_img` is the icon LVGL object (or nullptr if no icon).
    // Standard labels (top/bottom) and icon are already created by PadScreen.
    void (*createUI)(lv_obj_t* tile, const WidgetConfig* cfg,
                     const struct ScreenButtonConfig* btn,
                     const PadRect* rect, const UIScaleInfo* scale,
                     lv_obj_t* icon_img, WidgetState* state);

    // Update widget visuals from a new MQTT value string.
    // Called from PadScreen::pollMqttBindings() when the bound topic changes.
    // `raw_value` is the extracted (post-JSON-path) string value.
    void (*update)(lv_obj_t* tile, const WidgetConfig* cfg,
                   WidgetState* state, const char* raw_value);

    // Clean up any resources in WidgetState before tile deletion.
    // LVGL child objects are deleted automatically when the tile is deleted.
    void (*destroyUI)(WidgetState* state);

    // Optional periodic tick called every poll cycle, even when the bound
    // value hasn't changed.  Used by widgets that need periodic refresh
    // from external data (e.g. sparkline reads from data stream registry).
    // May be NULL.
    void (*tick)(lv_obj_t* tile, const WidgetConfig* cfg, WidgetState* state);

    // Optional: return data stream requirements for background collection.
    // Widgets that need historical data (ring buffers) implement this so the
    // data_stream registry can collect data independently of the active screen.
    // Called with stream_index 0, 1, 2, ... until it returns false.
    // Index maps to cfg->data_binding[stream_index].
    // Returns true if stream_index is valid and needs a data stream.
    // May be NULL (widget needs no data streams).
    bool (*getStreamParams)(const WidgetConfig* cfg, uint8_t stream_index,
                            uint16_t* window_secs, uint8_t* slot_count,
                            const char** out_binding);
};

// Look up a widget type by name. Returns NULL for "" or unknown types.
const WidgetType* widget_find(const char* type_name);

// Register a widget type at startup (called from each widget .cpp via auto-registration).
void widget_register(const WidgetType* type);

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
#if HAS_MQTT
    if (binding_template_has_bindings(s)) {
        char resolved[BINDING_TEMPLATE_MAX_LEN];
        binding_template_resolve(s, resolved, sizeof(resolved));
        char* end = nullptr;
        float val = strtof(resolved, &end);
        return (end == resolved) ? def : val;
    }
#endif
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
#define REGISTER_WIDGET(prefix, stream_fn)                                     \
    static const WidgetType prefix##_widget_type = {                           \
        #prefix, prefix##_parse, prefix##_create, prefix##_update,             \
        prefix##_destroy, prefix##_tick, stream_fn                             \
    };                                                                         \
    static struct prefix##AutoReg {                                            \
        prefix##AutoReg() { widget_register(&prefix##_widget_type); }          \
    } _##prefix##_auto_reg

#endif // HAS_DISPLAY
