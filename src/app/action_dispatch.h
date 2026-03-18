#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include "pad_config.h"  // ButtonAction, ACTION_TYPE_* constants

// Execute a ButtonAction — shared dispatch used by both pad button taps
// and swipe gesture actions.  Runs in LVGL task context.
// label: short prefix for log messages (e.g. "Tap", "LP", "SwipeR")
void action_dispatch(const ButtonAction& act, const char* label);

#endif // HAS_DISPLAY
