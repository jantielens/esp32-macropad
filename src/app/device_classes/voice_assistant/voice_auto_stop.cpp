#include "voice_auto_stop.h"

void voice_auto_stop_init(VoiceAutoStopDetector* detector, uint16_t silence_ms,
                          uint8_t speech_threshold) {
    if (!detector) return;
    detector->silence_ms = silence_ms;
    detector->speech_threshold = speech_threshold;
    detector->trailing_silence_ms = 0;
    detector->speech_detected = false;
}

bool voice_auto_stop_update(VoiceAutoStopDetector* detector, uint8_t rms_level,
                            uint32_t elapsed_ms) {
    if (!detector) return false;
    const uint8_t threshold = detector->speech_threshold ? detector->speech_threshold : 1;
    if (rms_level >= threshold) {
        detector->speech_detected = true;
        detector->trailing_silence_ms = 0;
        return false;
    }
    if (!detector->speech_detected) return false;
    const uint32_t remaining = UINT32_MAX - detector->trailing_silence_ms;
    detector->trailing_silence_ms += elapsed_ms < remaining ? elapsed_ms : remaining;
    return detector->trailing_silence_ms >= detector->silence_ms;
}