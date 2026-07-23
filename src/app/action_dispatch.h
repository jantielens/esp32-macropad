#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "pad_config.h"  // ButtonAction, ACTION_TYPE_* constants

// Execute a ButtonAction — shared dispatch used by both pad button taps
// and swipe gesture actions.  Runs in LVGL task context.
// label: short prefix for log messages (e.g. "Tap", "LP", "SwipeR")
void action_dispatch(const ButtonAction& act, const char* label);

// Process deferred operations (NVS writes) — call from main loop().
void action_dispatch_loop();

#if HAS_MQTT
// Collect MQTT topics referenced by an action's bindable fields into a
// binding_template_collect_topics() context, so tokens used only inside
// button actions (e.g. a bound visual-alert color) get subscribed. Mirrors
// the bindable-field set that action bindings resolve at dispatch time.
void action_collect_binding_topics(const ButtonAction& act, void* user_data);
#endif

#endif // HAS_DISPLAY || HAS_BUTTON
