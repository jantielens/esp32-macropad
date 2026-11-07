#pragma once

#include "widget.h"

#if HAS_DISPLAY

void option_selector_attach(WidgetState* state, const ButtonAction* actions, uint8_t count,
                            uint8_t page, bool overlay, uint8_t col, uint8_t row,
                            lv_event_cb_t input_guard, void* context);

#endif
