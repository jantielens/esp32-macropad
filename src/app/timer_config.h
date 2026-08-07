#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include "pad_config.h"
#include "timer_engine.h"
#include <ArduinoJson.h>

// Per-timer device-level configuration
struct TimerSettings {
    ButtonAction expire_actions[TIMER_MAX_EXPIRE_ACTIONS];    // actions on expiry
    uint8_t    expire_action_count;                           // 0..TIMER_MAX_EXPIRE_ACTIONS
};

// Device-level timer configuration for all timers
struct TimerConfig {
    TimerSettings timers[TIMER_COUNT];
};

struct TimerExpirySnapshot {
    ButtonAction actions[TIMER_MAX_EXPIRE_ACTIONS];
    uint8_t count;
};

// Load device-level expiry settings. Runtime timer state is not modified.
void timer_config_init();

// Copy one slot's expiry settings while holding the Timer Config mutex.
bool timer_config_snapshot_expiry(uint8_t id, TimerExpirySnapshot* out);

// Serialize the normalized in-memory configuration into an existing object.
void timer_config_to_json(JsonObject root);

// Physical storage presence, independent of normalized config contents.
bool timer_config_exists();

// Strictly validate, normalize, persist, then atomically replace the RAM cache.
bool timer_config_save_raw(const uint8_t* json, size_t len);

#endif // HAS_DISPLAY
