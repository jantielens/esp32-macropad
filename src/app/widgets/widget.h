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
};

// Look up a widget type by name. Returns NULL for "" or unknown types.
const WidgetType* widget_find(const char* type_name);

#endif // HAS_DISPLAY
