#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include "pad_config.h"  // ButtonDefaults struct

// Load device-level button defaults from LittleFS. Call after pad_config_init().
void button_defaults_init();

// Get the current device-level button defaults (never null, all-empty if file missing).
const ButtonDefaults* button_defaults_get();

// Save raw JSON to LittleFS and update RAM cache. Returns true on success.
bool button_defaults_save_raw(const uint8_t* json, size_t len);

#endif // HAS_DISPLAY
