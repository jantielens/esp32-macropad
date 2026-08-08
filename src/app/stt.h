#pragma once

#include "board_config.h"

#if HAS_STT

#include <stddef.h>

enum SttStatus : uint8_t {
    STT_STATUS_IDLE,
    STT_STATUS_RECORDING,
    STT_STATUS_TRANSCRIBING,
    STT_STATUS_READY,
    STT_STATUS_ERROR,
};

struct SttSnapshot {
    SttStatus status;
    char text[384];
};

void stt_init();
bool stt_start_recording();
bool stt_stop_and_transcribe(const char* mqtt_topic = nullptr);
void stt_get_snapshot(SttSnapshot* out);
void stt_loop();

#else

inline void stt_init() {}
inline void stt_loop() {}

#endif // HAS_STT