#pragma once

#include "../board_config.h"

#if HAS_DISPLAY

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
// 3. Implement parse/create/update/destroy functions
// 4. Declare a const WidgetType and register it in widget.cpp

// WidgetConfig is defined in pad_config.h (type[16] + data[WIDGET_CONFIG_MAX_BYTES])

// Opaque per-tile widget runtime state (LVGL objects, cached values, etc.)
// Created by createUI(), freed by destroyUI(). Stored in ButtonTile.
#define WIDGET_STATE_MAX_BYTES 48

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
    // Index 0 uses cfg->data_binding; higher indices use data_binding_2/3.
    // Returns true if stream_index is valid and needs a data stream.
    // May be NULL (widget needs no data streams).
    bool (*getStreamParams)(const WidgetConfig* cfg, uint8_t stream_index,
                            uint16_t* window_secs, uint8_t* slot_count,
                            const char** out_binding);
};

// Look up a widget type by name. Returns NULL for "" or unknown types.
const WidgetType* widget_find(const char* type_name);

// ---- Shared color-parsing helper for widget config ----
// Accepts numeric JSON values, "#RRGGBB", or "0xRRGGBB" strings.
inline uint32_t widget_parse_color(JsonVariant v, uint32_t def) {
    if (v.isNull()) return def;
    if (v.is<unsigned long>() || v.is<long>()) return (uint32_t)v.as<unsigned long>();
    if (!v.is<const char*>()) return def;
    const char* s = v.as<const char*>();
    if (!s || !*s) return def;
    if (s[0] == '#') s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    char* end = nullptr;
    uint32_t val = strtoul(s, &end, 16);
    return (end == s) ? def : val;
}

#endif // HAS_DISPLAY
