#pragma once

#include "../board_config.h"

#if HAS_DISPLAY && HAS_PSRAM
#include "widget_registry.h"
#include <lvgl.h>

void text_reader_attach(WidgetState* state, lv_event_cb_t guard, void* context);
#endif
