#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "pad_config.h"  // ButtonAction, ACTION_TYPE_* constants

#if HAS_DISPLAY
#include "timer_command.h"
#endif

// Execute a ButtonAction — shared dispatch used by both pad button taps
// and swipe gesture actions.  Runs in LVGL task context.
// label: short prefix for log messages (e.g. "Tap", "LP", "SwipeR")
void action_dispatch(const ButtonAction& act, const char* label);

// Process deferred operations (NVS writes) — call from main loop().
void action_dispatch_loop();

#if HAS_MQTT
#include "binding_template.h"
#include <string.h>

inline bool action_resolve_binding_field(char* field, size_t len,
										 bool reject_overflow = false) {
	if (field[0] && binding_template_has_bindings(field)) {
		char resolved[BINDING_TEMPLATE_MAX_LEN];
		binding_template_resolve(field, resolved, sizeof(resolved));
		size_t resolved_len = strlen(resolved);
		if (reject_overflow && resolved_len >= len) return false;
		size_t copy_len = resolved_len < len - 1 ? resolved_len : len - 1;
		memcpy(field, resolved, copy_len);
		field[copy_len] = '\0';
	}
	return true;
}

// Collect MQTT topics referenced by an action's bindable fields into a
// binding_template_collect_topics() context, so tokens used only inside
// button actions (e.g. a bound visual-alert color) get subscribed. Mirrors
// the bindable-field set that action bindings resolve at dispatch time.
void action_collect_binding_topics(const ButtonAction& act, void* user_data);
#endif

#endif // HAS_DISPLAY || HAS_BUTTON
