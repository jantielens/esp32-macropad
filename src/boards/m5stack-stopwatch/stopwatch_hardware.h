#pragma once

#include "board_config.h"
#include <stdint.h>

struct AudioBoardControl;
enum class StopWatchChargingMode : uint8_t { Off = 0, Low = 1, High = 2 };
static_assert(STOPWATCH_CHARGING_MODE == 0 || STOPWATCH_CHARGING_MODE == 1 || STOPWATCH_CHARGING_MODE == 2,
			  "STOPWATCH_CHARGING_MODE must be 0 (off), 1 (low), or 2 (high)");
bool m5stack_stopwatch_init();
bool m5stack_stopwatch_ready();
bool m5stack_stopwatch_set_charging_mode(StopWatchChargingMode mode);
bool m5stack_stopwatch_audio_power(bool enabled);
bool m5stack_stopwatch_audio_mute(bool muted);
bool m5stack_stopwatch_battery_read(uint16_t& millivolts, bool& charging);
AudioBoardControl stopwatch_audio_control();