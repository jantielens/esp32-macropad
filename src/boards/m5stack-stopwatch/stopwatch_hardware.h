#pragma once

#include <stdint.h>

struct AudioBoardControl;
bool m5stack_stopwatch_init();
bool m5stack_stopwatch_ready();
bool m5stack_stopwatch_audio_power(bool enabled);
bool m5stack_stopwatch_audio_mute(bool muted);
bool m5stack_stopwatch_battery_read(uint16_t& millivolts, bool& charging);
AudioBoardControl stopwatch_audio_control();