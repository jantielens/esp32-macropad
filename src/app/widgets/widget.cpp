#include "widget.h"

#if HAS_DISPLAY

#include <string.h>

// Dynamic widget type registry (populated by auto-registration constructors)
static constexpr int MAX_WIDGET_TYPES = 8;
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

#endif // HAS_DISPLAY
