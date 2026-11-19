#include "board_config.h"

#if IS_VOICE_ASSISTANT

#include "action_registry.h"
#include "voice.h"
#include "voice_payload.h"

namespace {
const ActionArgumentDef voice_auto_stop_arguments[] = {
    {"silence_ms", "Trailing silence (ms)", ACTION_ARGUMENT_INTEGER,
     VOICE_AUTO_STOP_MIN_SILENCE_MS, VOICE_AUTO_STOP_MAX_SILENCE_MS, 0,
     true, "[var:silence_ms]", "number", nullptr, 0, false, nullptr, nullptr, 0,
     nullptr, VOICE_AUTO_STOP_DEFAULT_SILENCE_MS, true},
    {"speech_threshold", "Speech level threshold", ACTION_ARGUMENT_INTEGER,
     VOICE_AUTO_STOP_MIN_THRESHOLD, VOICE_AUTO_STOP_MAX_THRESHOLD, 0,
     true, "[var:speech_threshold]", "number", nullptr, 0, false, nullptr, nullptr, 0,
     nullptr, VOICE_AUTO_STOP_DEFAULT_THRESHOLD, true},
};
const ActionArgumentDef voice_speak_arguments[] = {
    {"text", "Text", ACTION_ARGUMENT_STRING, 0, 0, VOICE_TTS_TEXT_MAX_LEN - 1,
     true, "[var:message]", "text"},
    {"voice", "Azure voice override", ACTION_ARGUMENT_STRING, 0, 0, VOICE_TTS_VOICE_MAX_LEN - 1,
     true, "[var:voice]", "text", nullptr, 0, true},
    {"volume", "Volume override (%)", ACTION_ARGUMENT_INTEGER, 0, 100, 0,
     true, "[var:volume]", "number"},
};
ActionResult run_voice_start(const ActionResolvedArgument*, const char*, uint32_t) {
    return voice_start_recording() ? ACTION_COMPLETE : ACTION_FAILED;
}
ActionResult run_voice_transcribe(const ActionResolvedArgument*, const char*, uint32_t token) {
    return voice_stop_and_transcribe(token) == VOICE_STOP_PENDING ? ACTION_PENDING : ACTION_FAILED;
}
ActionResult run_voice_auto_stop(const ActionResolvedArgument* args, const char*, uint32_t token) {
    return voice_start_until_silence(static_cast<uint16_t>(args[0].integer),
                                     static_cast<uint8_t>(args[1].integer),
                                     token) ? ACTION_PENDING : ACTION_FAILED;
}
ActionResult run_voice_cancel(const ActionResolvedArgument*, const char*, uint32_t) {
    voice_cancel_recording();
    return ACTION_FAILED;
}
ActionResult run_voice_speak(const ActionResolvedArgument* args, const char*, uint32_t) {
    return voice_speak(args[0].text, args[1].text, static_cast<uint8_t>(args[2].integer))
        ? ACTION_COMPLETE : ACTION_FAILED;
}
const ActionOperationDef voice_operations[] = {
    {"voice.record.start", "Start voice recording", nullptr, 0, run_voice_start},
    {"voice.record.stop_transcribe", "Stop and transcribe", nullptr, 0, run_voice_transcribe,
     ACTION_EXECUTION_PAUSABLE},
    {"voice.record.until_silence", "Record until silence", voice_auto_stop_arguments, 2,
     run_voice_auto_stop, ACTION_EXECUTION_PAUSABLE},
    {"voice.record.cancel", "Cancel voice recording", nullptr, 0, run_voice_cancel},
    {"voice.speak", "Speak text", voice_speak_arguments, 3, run_voice_speak},
};

void voice_describe(JsonObject& out) {
    out["group"] = "Audio";
    out["label"] = "Voice Assistant";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kVoiceActionType, ACTION_TYPE_VOICE, voice_describe, nullptr, ACTION_EXECUTION_PAUSABLE,
    true, false, voice_operations, sizeof(voice_operations) / sizeof(voice_operations[0]));
} // namespace

#endif // IS_VOICE_ASSISTANT