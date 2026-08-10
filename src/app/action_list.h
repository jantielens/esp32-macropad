#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_continuation.h"
#include "pad_config.h"   // ButtonAction
#include <ArduinoJson.h>

// Parse a JSON array of action objects into out[0..max-1].
// Drops empty/typeless entries. When filter_literal_none is true, also drops
// the JSON action type "none". Returns the count actually populated.
// Zero-initializes the full out buffer before parsing.
uint8_t action_list_parse(JsonVariant v, ButtonAction* out, uint8_t max,
						  bool filter_literal_none = false);

// Dispatch a list of actions sequentially with a shared log label.
// Safe with count == 0 or actions == nullptr (no-op).
void action_list_dispatch(const ButtonAction* actions, uint8_t count, const char* label,
						  ActionContinuationOwner owner = ACTION_CONTINUATION_OWNER_LOOP);

// Resume a completed asynchronous action suffix. Call from each potential
// owner; only the task that started the action list can consume it.
void action_list_dispatch_continuation(
	ActionContinuationOwner owner = ACTION_CONTINUATION_OWNER_LOOP);

#endif // HAS_DISPLAY || HAS_BUTTON
