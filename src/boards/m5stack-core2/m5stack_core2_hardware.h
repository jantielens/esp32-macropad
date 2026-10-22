#pragma once

#include "audio_output_driver.h"
#include <stdint.h>

bool m5stack_core2_set_backlight_brightness(uint8_t brightness);
bool m5stack_core2_set_speaker(bool enabled);
bool m5stack_core2_battery_read(uint16_t& millivolts, bool& charging);
bool m5stack_core2_request_shutdown(uint32_t continuation_token);
void m5stack_core2_loop();
AudioBoardControl m5stack_core2_audio_control();