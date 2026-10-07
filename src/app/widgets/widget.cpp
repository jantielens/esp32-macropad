#include "widget.h"

#if HAS_DISPLAY

#include <string.h>

// Dynamic widget type registry (populated by auto-registration constructors)
static constexpr int MAX_WIDGET_TYPES = 16;
static const WidgetType* s_widget_types[MAX_WIDGET_TYPES] = {};
static int s_widget_count = 0;

void widget_register(const WidgetType* type) {
    if (!type || s_widget_count >= MAX_WIDGET_TYPES) return;
    s_widget_types[s_widget_count++] = type;
}

const WidgetType* widget_find(const char* type_name) {
    if (!type_name || !type_name[0]) return nullptr;
    for (int i = 0; i < s_widget_count; i++) {
        if (strcmp(s_widget_types[i]->name, type_name) == 0)
            return s_widget_types[i];
    }
    return nullptr;
}

uint8_t widget_count() { return (uint8_t)s_widget_count; }

const WidgetType* widget_at(uint8_t index) {
    return (index < s_widget_count) ? s_widget_types[index] : nullptr;
}

void widget_preview_catalog_emit(JsonArray catalog) {
    for (uint8_t index = 0; index < widget_count(); ++index) {
        const WidgetType* type = widget_at(index);
        if (!type || !type->preview) continue;
        const WidgetPreview& preview = *type->preview;
        JsonObject entry = catalog.createNestedObject();
        entry["type"] = type->name;
        entry["name"] = preview.name;
        entry["icon"] = preview.icon;
        if (preview.second_icon) entry["second_icon"] = preview.second_icon;
        if (preview.axis_field) entry["axis_field"] = preview.axis_field;
        if (preview.horizontal_icon) entry["horizontal_icon"] = preview.horizontal_icon;
        if (preview.vertical_icon) entry["vertical_icon"] = preview.vertical_icon;
        if (preview.default_axis) entry["default_axis"] = preview.default_axis;
    }
}

#endif // HAS_DISPLAY
