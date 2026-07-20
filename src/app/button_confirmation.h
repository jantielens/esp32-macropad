#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include "pad_config.h"

// Show a modal confirmation for a copied action list. Returns false if the
// prompt could not be created; callers must treat failure as cancellation.
bool button_confirmation_show(const ButtonAction* actions, uint8_t count,
                              const char* event_label, const char* confirm_text,
                              const char* button_label);

// Cancel an active prompt. Must be called from the LVGL task.
void button_confirmation_cancel();

#endif // HAS_DISPLAY