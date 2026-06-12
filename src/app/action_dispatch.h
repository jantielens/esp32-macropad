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

#endif // HAS_DISPLAY || HAS_BUTTON
