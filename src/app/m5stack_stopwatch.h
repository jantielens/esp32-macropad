#pragma once

#include "board_config.h"
#if HAS_M5STACK_STOPWATCH
#include <stddef.h>
#include <stdint.h>

bool m5stack_stopwatch_init();
bool m5stack_stopwatch_ready();
bool m5stack_stopwatch_audio_power(bool enabled);
bool m5stack_stopwatch_audio_mute(bool muted);
bool m5stack_stopwatch_touch_read(uint8_t* data, size_t length);
bool m5stack_stopwatch_battery_read(uint16_t& millivolts, bool& charging);
#endif
