#include "board_config.h"

#if IS_VOICE_ASSISTANT

#include "action_registry.h"
#include "voice.h"
#include "voice_payload.h"

#include <stdio.h>
#include <string.h>

namespace {
void voice_parse(const JsonObject& object, ButtonAction& action) {
    VoicePayload& payload = voice_payload(action);
    strlcpy(payload.command, object["command"] | "", sizeof(payload.command));
    payload.silence_ms = VOICE_AUTO_STOP_DEFAULT_SILENCE_MS;
    payload.speech_threshold = VOICE_AUTO_STOP_DEFAULT_THRESHOLD;
    if (strcmp(payload.command, "record_until_silence") == 0) {
        const uint32_t silence_ms = object["silence_ms"] | VOICE_AUTO_STOP_DEFAULT_SILENCE_MS;
        const uint32_t threshold = object["speech_threshold"] | VOICE_AUTO_STOP_DEFAULT_THRESHOLD;
        if (voice_auto_stop_settings_valid(silence_ms, threshold)) {
            payload.silence_ms = (uint16_t)silence_ms;
            payload.speech_threshold = (uint8_t)threshold;
        }
    }
}

void voice_serialize(const ButtonAction& action, JsonObject object) {
    const VoicePayload& payload = voice_payload(action);
    if (!payload.command[0]) return;
    object["command"] = payload.command;
    if (strcmp(payload.command, "record_until_silence") == 0) {
        object["silence_ms"] = payload.silence_ms;
        object["speech_threshold"] = payload.speech_threshold;
    }
}

ActionResult voice_dispatch(const ButtonAction& action, const char*, uint32_t continuation_token) {
    const char* command = voice_payload(action).command;
    if (strcmp(command, "record_start") == 0) {
        return voice_start_recording() ? ACTION_COMPLETE : ACTION_FAILED;
    }
    if (strcmp(command, "record_stop_transcribe") == 0) {
        const VoiceStopResult result = voice_stop_and_transcribe(continuation_token);
        return result == VOICE_STOP_PENDING ? ACTION_PENDING : ACTION_FAILED;
    }
    if (strcmp(command, "record_until_silence") == 0) {
        const VoicePayload& payload = voice_payload(action);
        return voice_start_until_silence(payload.silence_ms, payload.speech_threshold,
                                         continuation_token) ? ACTION_PENDING : ACTION_FAILED;
    }
    if (strcmp(command, "record_cancel") == 0) {
        voice_cancel_recording();
        return ACTION_FAILED;
    }
    return ACTION_FAILED;
}

void voice_describe(JsonObject& out) {
    out["group"] = "Audio";
    out["label"] = "Voice Assistant";
    out["command_field"] = "command";
    JsonArray commands = out.createNestedArray("commands");
    JsonObject start = commands.createNestedObject();
    start["id"] = "record_start";
    start["label"] = "Start recording";
    JsonObject stop = commands.createNestedObject();
    stop["id"] = "record_stop_transcribe";
    stop["label"] = "Stop and transcribe";
    JsonObject auto_stop = commands.createNestedObject();
    auto_stop["id"] = "record_until_silence";
    auto_stop["label"] = "Record until silence";
    JsonObject cancel = commands.createNestedObject();
    cancel["id"] = "record_cancel";
    cancel["label"] = "Cancel recording";
    JsonArray fields = out.createNestedArray("fields");
    JsonObject silence = fields.createNestedObject();
    silence["name"] = "silence_ms";
    char silence_description[112] = {};
    snprintf(silence_description, sizeof(silence_description),
             "trailing silence in milliseconds for record_until_silence (%u-%u, default %u)",
             VOICE_AUTO_STOP_MIN_SILENCE_MS, VOICE_AUTO_STOP_MAX_SILENCE_MS,
             VOICE_AUTO_STOP_DEFAULT_SILENCE_MS);
    silence["description"] = silence_description;
    JsonObject threshold = fields.createNestedObject();
    threshold["name"] = "speech_threshold";
    char threshold_description[128] = {};
    snprintf(threshold_description, sizeof(threshold_description),
             "speech level threshold on the %u-%u audio input RMS scale for record_until_silence (default %u)",
             VOICE_AUTO_STOP_MIN_THRESHOLD, VOICE_AUTO_STOP_MAX_THRESHOLD,
             VOICE_AUTO_STOP_DEFAULT_THRESHOLD);
    threshold["description"] = threshold_description;
}

const ActionTypeDef kVoiceActionType = {
    /* type_name   */ ACTION_TYPE_VOICE,
    /* parse       */ voice_parse,
    /* serialize   */ voice_serialize,
    /* dispatch    */ voice_dispatch,
    /* value_field */ nullptr,
    /* describe    */ voice_describe,
};

REGISTER_ACTION_TYPE(kVoiceActionType);
} // namespace

#endif // IS_VOICE_ASSISTANT