#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_result.h"
#include "pad_config.h"  // ButtonAction, ACTION_TYPE_* constants

// Execute a ButtonAction — shared dispatch used by both pad button taps
// and swipe gesture actions. Existing actions return ACTION_COMPLETE even when
// they log an operational warning, preserving historical action-array behavior.
// An asynchronous action returns ACTION_PENDING and later uses its token to
// resume the copied suffix on the task that dispatched the action list.
ActionResult action_dispatch(const ButtonAction& act, const char* label,
							 uint32_t continuation_token = 0, bool interactive = false);

struct ActionDispatchContext {
	bool synchronous = false;
	bool (*work_guard)(uint32_t) = nullptr;
	uint32_t generation = 0;
	bool alarm_audio = false;
	bool interactive = false;
};
ActionDispatchContext action_dispatch_context();
ActionResult action_dispatch_synchronous(const ButtonAction& act, const char* label,
										 bool (*work_guard)(uint32_t), uint32_t generation,
										 bool alarm_audio = false);

// Process deferred action work and loop-owned action continuations — call from loop().
void action_dispatch_loop();

#if HAS_MQTT || HAS_HA_LIVE
// Collect MQTT topics referenced by an action's bindable fields into a
// binding_template_collect_topics() context, so tokens used only inside
// button actions (e.g. a bound visual-alert color) get subscribed. Mirrors
// the bindable-field set that action bindings resolve at dispatch time.
void action_collect_binding_topics(const ButtonAction& act, void* user_data);
#endif

#endif // HAS_DISPLAY || HAS_BUTTON
