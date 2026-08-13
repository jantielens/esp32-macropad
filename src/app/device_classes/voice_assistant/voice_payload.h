#pragma once

#include "board_config.h"

#if IS_VOICE_ASSISTANT

#include "../../pad_config.h"

#define ACTION_TYPE_VOICE "voice"
#define VOICE_AUTO_STOP_DEFAULT_SILENCE_MS 1000U
#define VOICE_AUTO_STOP_DEFAULT_THRESHOLD 2U
#define VOICE_AUTO_STOP_MIN_SILENCE_MS 100U
#define VOICE_AUTO_STOP_MAX_SILENCE_MS 10000U
#define VOICE_AUTO_STOP_MIN_THRESHOLD 0U
#define VOICE_AUTO_STOP_MAX_THRESHOLD 100U
#define VOICE_TTS_TEXT_MAX_LEN 60
#define VOICE_TTS_VOICE_MAX_LEN 8

struct VoicePayload {
    char command[24];
    uint16_t silence_ms;
    uint8_t speech_threshold;
    char text[VOICE_TTS_TEXT_MAX_LEN];
    char voice[VOICE_TTS_VOICE_MAX_LEN];
    uint8_t volume;
};

inline bool voice_auto_stop_settings_valid(uint32_t silence_ms, uint32_t speech_threshold) {
    return silence_ms >= VOICE_AUTO_STOP_MIN_SILENCE_MS &&
            silence_ms <= VOICE_AUTO_STOP_MAX_SILENCE_MS &&
            speech_threshold >= VOICE_AUTO_STOP_MIN_THRESHOLD &&
            speech_threshold <= VOICE_AUTO_STOP_MAX_THRESHOLD;
}

static_assert(sizeof(VoicePayload) <= ACTION_PAYLOAD_DEVICE_CLASS_BYTES,
              "VoicePayload exceeds ACTION_PAYLOAD_DEVICE_CLASS_BYTES");

inline VoicePayload& voice_payload(ButtonAction& action) {
    return *reinterpret_cast<VoicePayload*>(action.payload.device_class);
}

inline const VoicePayload& voice_payload(const ButtonAction& action) {
    return *reinterpret_cast<const VoicePayload*>(action.payload.device_class);
}

#endif // IS_VOICE_ASSISTANT