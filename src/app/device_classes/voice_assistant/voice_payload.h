#pragma once

#include "board_config.h"

#if IS_VOICE_ASSISTANT

#include "../../pad_config.h"

#define ACTION_TYPE_VOICE "voice"

struct VoicePayload {
    char command[24];
};

static_assert(sizeof(VoicePayload) <= ACTION_PAYLOAD_DEVICE_CLASS_BYTES,
              "VoicePayload exceeds ACTION_PAYLOAD_DEVICE_CLASS_BYTES");

inline VoicePayload& voice_payload(ButtonAction& action) {
    return *reinterpret_cast<VoicePayload*>(action.payload.device_class);
}

inline const VoicePayload& voice_payload(const ButtonAction& action) {
    return *reinterpret_cast<const VoicePayload*>(action.payload.device_class);
}

#endif // IS_VOICE_ASSISTANT