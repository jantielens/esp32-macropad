#pragma once

#include "board_config.h"

#if IS_SHUTTER_TESTER

#include "pad_config.h"  // ButtonAction, ACTION_TYPE_SHUTTER, MAX_BUTTON_ACTIONS

#include <string.h>

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

// Get the current cached config (never null; empty if file missing).
const ShutterSessionActionsConfig* shutter_session_actions_get();

// Save raw JSON to flash and refresh the RAM cache. Returns true on success.
bool shutter_session_actions_save_raw(const uint8_t* json, size_t len);

// Dispatch all configured save_start actions sequentially.
// Safe to call from the LVGL/action-dispatch task.
void shutter_session_actions_dispatch_start();

// Mark the save_complete event from the background persist task.
// Safe to call from any task — sets an atomic flag drained by _loop() on the LVGL task.
void shutter_session_actions_notify_complete();

// Drain any pending save_complete notification and dispatch the configured actions.
// Must be called from the LVGL/main loop task (next to message_bubble_loop()).
void shutter_session_actions_loop();

// Self-trigger guard helper (exposed for tests, inline so test builds don't
// need to link the full module). Returns true when the action would re-enter
// session start/stop and must be skipped.
inline bool shutter_session_actions_is_self_trigger(const ButtonAction& act) {
    if (strcmp(act.type, ACTION_TYPE_SHUTTER) != 0) return false;
    return strcmp(act.shutter_command, "sess_stop")  == 0
        || strcmp(act.shutter_command, "sess_start") == 0;
}

#endif // IS_SHUTTER_TESTER
