#pragma once

#include <stdbool.h>
#include <stdint.h>

struct VoiceAutoStopDetector {
    uint16_t silence_ms;
    uint8_t speech_threshold;
    uint32_t trailing_silence_ms;
    bool speech_detected;
};

void voice_auto_stop_init(VoiceAutoStopDetector* detector, uint16_t silence_ms,
                          uint8_t speech_threshold);
bool voice_auto_stop_update(VoiceAutoStopDetector* detector, uint8_t rms_level,
                            uint32_t elapsed_ms);