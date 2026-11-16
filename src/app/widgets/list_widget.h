#pragma once

#include "../board_config.h"

#if HAS_DISPLAY && HAS_PSRAM
#include "widget_registry.h"
#include <lvgl.h>

// Called on the LVGL task, with the current pad active.
bool list_widget_command(WidgetState* state, const char* command);

// Install the owning pad's input guard on every current and future row.
void list_widget_attach(WidgetState* state, lv_event_cb_t guard, void* context);
#endif
