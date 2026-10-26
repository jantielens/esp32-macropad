#pragma once

#include "widget_registry.h"

void slider_cancel_input(WidgetState* runtime);
void slider_attach(WidgetState* runtime, const ButtonAction* actions, uint8_t count,
				   lv_event_cb_t input_guard, void* context);