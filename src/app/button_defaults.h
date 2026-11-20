#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include "pad_config.h"  // ButtonDefaults struct
#include <ArduinoJson.h>

// Load device-level pad and button defaults after storage mounts, before caching pads.
void button_defaults_init();

// Get device defaults (never null; unset numeric settings use firmware defaults).
const ButtonDefaults* button_defaults_get();

// Device-wide burn-in pixel shift distance. Zero disables shifting.
uint8_t button_defaults_get_pixel_shift_distance();

// Missing overrides are an empty object; read or parse errors return false.
bool button_defaults_load_overrides(JsonDocument& doc);
const char* button_defaults_validate(JsonObjectConst config);
void button_defaults_typography_status(JsonObject status);

// Save raw JSON to LittleFS and update RAM cache. Returns true on success.
bool button_defaults_save_raw(const uint8_t* json, size_t len);

#endif // HAS_DISPLAY
