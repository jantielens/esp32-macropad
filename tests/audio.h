// Test stub: audio.h — records audio_beep() calls for assertion
#pragma once

#include <cstdint>

struct AudioBeepCall {
    char pattern[48];
    uint8_t volume;
};

// Recorded calls — test code reads these
#define AUDIO_BEEP_LOG_MAX  32
extern AudioBeepCall g_beep_log[];
extern int           g_beep_log_count;

void audio_beep(const char* pattern, uint8_t volume_override);

inline void audio_init(uint8_t) {}
inline void audio_stop() {}
