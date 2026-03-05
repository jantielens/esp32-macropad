#include "widget.h"

#if HAS_DISPLAY

#include <string.h>

// Forward declarations for widget type instances
extern const WidgetType bar_chart_widget_type;

// Static registry of all widget types (NULL-terminated)
static const WidgetType* s_widget_types[] = {
    &bar_chart_widget_type,
    // Future: &gauge_widget_type,
    // Future: &sparkline_widget_type,
    nullptr
};

const WidgetType* widget_find(const char* type_name) {
    if (!type_name || !type_name[0]) return nullptr;
    for (int i = 0; s_widget_types[i]; i++) {
        if (strcmp(s_widget_types[i]->name, type_name) == 0)
            return s_widget_types[i];
    }
    return nullptr;
}

#endif // HAS_DISPLAY
