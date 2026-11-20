#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_continuation.h"
#include "action_dispatch.h"
#include "pad_config.h"   // ButtonAction
#include <ArduinoJson.h>
#if HAS_MCP
#include "action_run_context.h"
#endif

// Parse a JSON array of action objects into out[0..max-1].
// Returns the count actually populated and zero-initializes the output buffer.
uint8_t action_list_parse(JsonVariant v, ButtonAction* out, uint8_t max);

bool action_list_has_audio(const ButtonAction* actions, uint8_t count);

// Dispatch a list of actions sequentially with a shared log label.
// Safe with count == 0 or actions == nullptr (no-op).
void action_list_dispatch(const ButtonAction* actions, uint8_t count, const char* label,
						  ActionContinuationOwner owner = ACTION_CONTINUATION_OWNER_LOOP,
						  bool interactive = false);
void action_list_dispatch(const ButtonAction* actions, uint8_t count, const char* label,
                          ActionContinuationOwner owner, bool interactive,
                          const PadBinding* origin_bindings, uint8_t origin_binding_count);
ActionResult action_list_dispatch_result(const ButtonAction* actions, uint8_t count, const char* label,
                                         ActionContinuationOwner owner = ACTION_CONTINUATION_OWNER_LOOP,
                                         bool interactive = false);
ActionResult action_list_dispatch_result(const ButtonAction* actions, uint8_t count, const char* label,
                                         ActionContinuationOwner owner, bool interactive,
                                         const PadBinding* origin_bindings, uint8_t origin_binding_count);
#if HAS_MCP
ActionResult action_list_dispatch_observed(const ButtonAction* actions, uint8_t count, const char* label,
                                          ActionContinuationOwner owner, bool interactive,
                                          const PadBinding* origin_bindings, uint8_t origin_binding_count,
                                          const ActionRunObserver& observer);
#endif

// Resume a completed asynchronous action suffix. Call from each potential
// owner; only the task that started the action list can consume it.
void action_list_dispatch_continuation(
	ActionContinuationOwner owner = ACTION_CONTINUATION_OWNER_LOOP);

#endif // HAS_DISPLAY || HAS_BUTTON
