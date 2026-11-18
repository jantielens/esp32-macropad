#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include "pad_config.h"
#include <ArduinoJson.h>

struct ActionStorage;

enum SwipeDirection : uint8_t {
    SWIPE_LEFT,
    SWIPE_RIGHT,
    SWIPE_UP,
    SWIPE_DOWN,
};

// Swipe action configuration — 4 directions, each a standard ButtonAction
struct SwipeConfig {
    ButtonAction swipe_left;
    ButtonAction swipe_right;
    ButtonAction swipe_up;
    ButtonAction swipe_down;
};

// Load swipe config from LittleFS. Call after pad_config_init().
void swipe_config_init();

// Serialize or copy under the cache lock; copied operation arguments are owned
// by storage and remain valid until action_storage_clear(storage).
bool swipe_config_to_json(JsonObject object);
bool swipe_config_copy_action(SwipeDirection direction, ButtonAction& action, ActionStorage& storage);

// Validate and atomically save JSON before replacing the RAM cache.
bool swipe_config_save_raw(const uint8_t* json, size_t len);

// Reload from flash into RAM cache (e.g. after external file change).
void swipe_config_reload();

#endif // HAS_DISPLAY
