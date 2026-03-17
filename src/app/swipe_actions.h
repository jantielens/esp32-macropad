#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include <lvgl.h>

// Register the global swipe gesture handler on an LVGL screen object.
// Call from any Screen::create() to enable configurable swipe actions.
// The handler reads from swipe_config and dispatches via the standard
// action system (screen nav, back, MQTT, BLE key, etc.).
void swipe_actions_register(lv_obj_t* screen_obj);

// Timestamp of last swipe event (milliseconds, from lv_tick_get()).
// Checked by button tap/long-press handlers to suppress accidental
// taps that LVGL fires as part of the same gesture touch sequence.
uint32_t swipe_actions_last_swipe_time();

#endif // HAS_DISPLAY
