#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include "pad_config.h"       // ButtonAction, MAX_BUTTON_ACTIONS
#include "timer_engine.h"     // TIMER_COUNT, TIMER_MAX_EXPIRE_ACTIONS, TimerMode

// Per-timer device-level configuration
struct TimerSettings {
    TimerMode  mode;                                          // up or down
    uint32_t   countdown;                                     // seconds (countdown only)
    ButtonAction expire_actions[TIMER_MAX_EXPIRE_ACTIONS];    // actions on expiry
    uint8_t    expire_action_count;                           // 0..TIMER_MAX_EXPIRE_ACTIONS
};

// Device-level timer configuration for all timers
struct TimerConfig {
    TimerSettings timers[TIMER_COUNT];
};

// Load timer config from LittleFS and apply to timer engine. Call after timer_binding_init().
void timer_config_init();

// Get the current timer config (never null, returns defaults if file missing).
const TimerConfig* timer_config_get();

// Save raw JSON to LittleFS, update RAM cache, and re-apply to timer engine.
// Returns true on success.
bool timer_config_save_raw(const uint8_t* json, size_t len);

#endif // HAS_DISPLAY
