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
bool m5stack_stopwatch_set_status_led(bool enabled);
bool m5stack_stopwatch_vibrate(uint32_t duration_ms);
constexpr uint8_t STOPWATCH_VIBRATION_MAX_STEPS = 15;
bool m5stack_stopwatch_vibrate_pattern(const uint16_t* durations, uint8_t count);
bool m5stack_stopwatch_request_shutdown(uint32_t continuation_token);
void m5stack_stopwatch_loop();
bool m5stack_stopwatch_audio_power(bool enabled);
bool m5stack_stopwatch_audio_mute(bool muted);
bool m5stack_stopwatch_battery_read(uint16_t& millivolts, bool& charging);
AudioBoardControl stopwatch_audio_control();