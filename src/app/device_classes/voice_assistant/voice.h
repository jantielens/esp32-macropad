#pragma once

#include "board_config.h"

#if IS_VOICE_ASSISTANT

#include <stddef.h>
#include <stdint.h>

enum VoiceStatus : uint8_t {
    VOICE_STATUS_IDLE,
    VOICE_STATUS_RECORDING,
    VOICE_STATUS_LISTENING,
    VOICE_STATUS_TRANSCRIBING,
    VOICE_STATUS_READY,
    VOICE_STATUS_ERROR,
};

enum VoiceStopResult : uint8_t {
    VOICE_STOP_FAILED,
    VOICE_STOP_PENDING,
    VOICE_STOP_AUTO_CONTINUATION,
};

struct VoiceSnapshot {
    VoiceStatus status;
    char text[384];
};

void voice_init();
bool voice_start_recording();
bool voice_start_until_silence(uint16_t silence_ms, uint8_t speech_threshold,
                               uint32_t continuation_token);
VoiceStopResult voice_stop_and_transcribe(uint32_t continuation_token);
bool voice_cancel_recording();
void voice_get_snapshot(VoiceSnapshot* out);
const char* voice_status_name(VoiceStatus status);
bool voice_api_key_configured();

#endif // IS_VOICE_ASSISTANT