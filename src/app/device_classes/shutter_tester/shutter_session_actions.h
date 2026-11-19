#pragma once

#include "board_config.h"

#if IS_SHUTTER_TESTER

#include "../../pad_config.h"  // ButtonAction, MAX_BUTTON_ACTIONS
#include <string>

// Lifecycle actions dispatched around shutter session persistence.
// Each event holds up to MAX_BUTTON_ACTIONS (3) actions executed sequentially,
// mirroring boot_actions / timer expire actions.
// A count of 0 means "no actions" (preserves prior silent behavior).
struct ShutterSessionActionsConfig {
    ButtonAction save_start_actions[MAX_BUTTON_ACTIONS];     // Dispatched when shutter_session_stop() begins persistence
    uint8_t      save_start_count;
    ButtonAction save_complete_actions[MAX_BUTTON_ACTIONS];  // Dispatched after persist_session() returns
    uint8_t      save_complete_count;
};

// Load config from /config/shutter_session_actions.json. Call once at boot.
void shutter_session_actions_init();

// Serialize under lock; returns false for an incompatible saved document.
bool shutter_session_actions_to_json(std::string& out);

// Save raw JSON to flash and refresh the RAM cache. Returns true on success.
bool shutter_session_actions_save_raw(const uint8_t* json, size_t len);

// Dispatch all configured save_start actions sequentially.
// Safe to call from the LVGL/action-dispatch task.
void shutter_session_actions_dispatch_start();

// Mark the save_complete event from the background persist task.
// Safe to call from any task — sets an atomic flag drained by _loop() on the LVGL task.
void shutter_session_actions_notify_complete();

// Drain any pending save_complete notification and dispatch the configured actions.
// Must be called from the LVGL/main loop task.
void shutter_session_actions_loop();

#endif // IS_SHUTTER_TESTER
