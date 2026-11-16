#pragma once

#include "../board_config.h"

#if HAS_DISPLAY && HAS_PSRAM

#include "../list_source.h"
#include "widget_registry.h"
#include <string.h>

struct PadBinding;

inline bool list_identifier_valid(const char* id) {
    if (!id || !*id || strlen(id) > 32) return false;
    for (const char* p = id; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
    }
    return true;
}

enum class ListViewport : uint8_t { Scroll, Paged };

struct ListConfig {
    const ListSourceDescriptor* source;
    char id[33];
    char label_top[CONFIG_LABEL_MAX_LEN];
    char label_bottom[CONFIG_LABEL_MAX_LEN];
    char style_top[CONFIG_LABEL_STYLE_MAX_LEN];
    char style_bottom[CONFIG_LABEL_STYLE_MAX_LEN];
    char background[CONFIG_COLOR_MAX_LEN];
    char border_color[CONFIG_COLOR_MAX_LEN];
    char border_width[CONFIG_BINDABLE_SHORT_LEN];
    char default_border_width[CONFIG_BINDABLE_SHORT_LEN];
    char default_border_color[CONFIG_COLOR_MAX_LEN];
    char default_foreground[CONFIG_COLOR_MAX_LEN];
    char selected_background[CONFIG_COLOR_MAX_LEN];
    char selected_border_color[CONFIG_COLOR_MAX_LEN];
    char selected_top_color[CONFIG_COLOR_MAX_LEN];
    char selected_bottom_color[CONFIG_COLOR_MAX_LEN];
    char indicator_track_color[CONFIG_COLOR_MAX_LEN];
    char indicator_color[CONFIG_COLOR_MAX_LEN];
    int8_t selected_border_width;
    uint8_t visible_items;
    uint8_t padding;
    uint8_t spacing;
    uint8_t radius;
    ListViewport viewport;
    bool allow_selection;
};

// Validate with composed pad bindings before parsing; ordinary scalar binding
// and action validation must also run, excluding the typed source field.
const char* list_validate_config(JsonObjectConst button, const PadBinding* bindings = nullptr,
                                 uint8_t binding_count = 0);
const char* list_validate_actions(JsonObjectConst button, const PadBinding* bindings,
                                  uint8_t binding_count);
// Set WidgetType::validatePadContext to this at List cutover.
const char* list_validate_pad_context(JsonObjectConst pad, const PadComposedContext& context);
bool list_parse_config(const JsonObject& button, uint8_t* data);
void list_retain_config(uint8_t* data);
void list_release_config(uint8_t* data);
const ListConfig* list_config(const uint8_t* data);
void list_visit_resources(const uint8_t* data, WidgetResourceVisitor visitor, void* context);

#endif
