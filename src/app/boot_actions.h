#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include "pad_config.h"  // ButtonAction, MAX_BUTTON_ACTIONS

// Boot actions configuration — up to MAX_BUTTON_ACTIONS sequential actions
// dispatched once when the first screen is shown after boot.
struct BootActionsConfig {
    ButtonAction actions[MAX_BUTTON_ACTIONS];
    uint8_t action_count;
};

// Load boot actions from LittleFS. Call after pad_config_init().
void boot_actions_init();

// Get the current boot actions config (never null, returns empty if file missing).
const BootActionsConfig* boot_actions_get();

// Save raw JSON to LittleFS and update RAM cache. Returns true on success.
bool boot_actions_save_raw(const uint8_t* json, size_t len);

// Dispatch boot actions via action_dispatch(). Call once after first screen is shown.
void boot_actions_dispatch();

#endif // HAS_DISPLAY
