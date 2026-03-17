#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include "pad_config.h"  // ButtonAction struct, ACTION_TYPE_* constants

// Swipe action configuration — 4 directions, each a standard ButtonAction
struct SwipeConfig {
    ButtonAction swipe_left;
    ButtonAction swipe_right;
    ButtonAction swipe_up;
    ButtonAction swipe_down;
};

// Load swipe config from LittleFS. Call after pad_config_init().
void swipe_config_init();

// Get the current swipe config (never null, returns defaults if file missing).
const SwipeConfig* swipe_config_get();

// Save raw JSON to LittleFS and update RAM cache. Returns true on success.
bool swipe_config_save_raw(const uint8_t* json, size_t len);

// Reload from flash into RAM cache (e.g. after external file change).
void swipe_config_reload();

#endif // HAS_DISPLAY
