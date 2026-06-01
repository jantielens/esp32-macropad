#pragma once

#include <Arduino.h>
#include <cstdint>

// ============================================================================
// Relay Controller — action-based enlarger/safelight relay control
// ============================================================================
// Compile-time gated by IS_DARKROOM_TIMER (the only translation unit that
// #includes this header is itself aggregated under that gate, so a single
// definition serves the whole build).
//
// Stores 4 ButtonAction slots in a JSON file on the Storage facade:
//   slot 0: enlarger ON
//   slot 1: enlarger OFF
//   slot 2: safelight ON
//   slot 3: safelight OFF
//
// For type="shelly", the relay FreeRTOS task executes HTTP directly.
// For all other action types, execution is deferred to relay_loop()
// on the main loop() via action_dispatch().
//
// Thread model:
//   relay_request()  — safe from any task (sets bitmask + signals relay task)
//   relay_is_on()    — safe from any task (reads guarded state)
//   relay_loop()     — call from main loop() (dispatches deferred actions)

#define RELAY_SLOT_COUNT 4

// Initialize the relay controller. Call once during setup().
void relay_controller_init();

// Load relay action config from the storage facade. Call after pad config init.
void relay_load_config();

// Request relay state change. Safe to call from LVGL task context.
// on=true → fires enlarger ON + safelight OFF, on=false → the inverse.
void relay_request(bool on);

// Current relay state (last requested).
bool relay_is_on();

// Process deferred relay actions. Call from main loop().
void relay_loop();

// Delete relay config file and reset cached actions. Called from factory reset.
void relay_controller_clear_config();

// Queue a Shelly HTTP relay command for execution on the relay task.
// Safe to call from any task (LVGL, main loop, etc.). Non-blocking.
void relay_queue_shelly(const char* host, uint8_t relay_index, bool on);

// Serialize current relay config as JSON into `out`.
bool relay_get_config_json(String& out);

// Write raw JSON to config file and hot-reload.
bool relay_save_config_from_json(const uint8_t* json, size_t len);
