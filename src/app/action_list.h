#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include "pad_config.h"   // ButtonAction
#include <ArduinoJson.h>

// Parse a JSON array of action objects into out[0..max-1].
// Drops empty/typeless entries. Returns the count actually populated.
// Zero-initializes the full out buffer before parsing.
uint8_t action_list_parse(JsonVariant v, ButtonAction* out, uint8_t max);

// Dispatch a list of actions sequentially with a shared log label.
// Safe with count == 0 or actions == nullptr (no-op).
void action_list_dispatch(const ButtonAction* actions, uint8_t count, const char* label);

#endif // HAS_DISPLAY
