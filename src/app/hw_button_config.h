#pragma once

#include "board_config.h"

#if HAS_BUTTON

#include "pad_config.h"  // ButtonAction, MAX_BUTTON_ACTIONS

// ============================================================================
// Hardware Button Config — per-button tap/hold action lists
// ============================================================================
// Persisted to LittleFS at /config/hw_buttons.json. One entry per declared
// button (HW_BUTTON_DEFS). Each button has up to MAX_BUTTON_ACTIONS tap
// actions and MAX_BUTTON_ACTIONS hold actions, mirroring on-screen buttons.

struct HwButtonConfig {
    ButtonAction tap_actions[MAX_BUTTON_ACTIONS];
    uint8_t tap_count;
    ButtonAction hold_actions[MAX_BUTTON_ACTIONS];
    uint8_t hold_count;
};

// Load config from LittleFS into the RAM cache. Defaults to empty (no actions)
// if the file is missing. Call after pad_config_init() / storage mount.
void hw_button_config_init();

// Get the cached config for button `index` (bounds-checked, returns nullptr if
// index >= NUM_HW_BUTTONS).
const HwButtonConfig* hw_button_config_get(uint8_t index);

// Save raw JSON to LittleFS and reload the RAM cache. Returns true on success.
bool hw_button_config_save_raw(const uint8_t* json, size_t len);

#else  // !HAS_BUTTON

// No-op stub so callers (app.ino) can invoke unconditionally.
inline void hw_button_config_init() {}

#endif  // HAS_BUTTON
