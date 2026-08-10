#include "board_config.h"

#if IS_VOICE_ASSISTANT

#include "action_registry.h"
#include "voice.h"
#include "voice_payload.h"

#include <string.h>

namespace {
void voice_parse(const JsonObject& object, ButtonAction& action) {
    strlcpy(voice_payload(action).command, object["command"] | "",
            sizeof(voice_payload(action).command));
}

void voice_serialize(const ButtonAction& action, JsonObject object) {
    if (voice_payload(action).command[0]) object["command"] = voice_payload(action).command;
}

ActionResult voice_dispatch(const ButtonAction& action, const char*, uint32_t continuation_token) {
    const char* command = voice_payload(action).command;
    if (strcmp(command, "record_start") == 0) {
        return voice_start_recording() ? ACTION_COMPLETE : ACTION_FAILED;
    }
    if (strcmp(command, "record_stop_transcribe") == 0) {
        return voice_stop_and_transcribe(continuation_token) ? ACTION_PENDING : ACTION_FAILED;
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