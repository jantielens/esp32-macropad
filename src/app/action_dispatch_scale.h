#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include "pad_config.h"

// Try to handle a scale or brew action. Returns true if the action was handled.
bool scale_action_dispatch(const ButtonAction& act, const char* label);

#endif // HAS_DISPLAY
