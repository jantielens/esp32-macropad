#include "board_config.h"

#if HAS_STT

#include "action_registry.h"
#include "log_manager.h"
#include "stt.h"

#include <string.h>

namespace {

void stt_parse(const JsonObject& object, ButtonAction& action) {
    strlcpy(action.payload.stt.stt_command, object["stt_command"] | "",
            sizeof(action.payload.stt.stt_command));
    strlcpy(action.payload.stt.stt_mqtt_topic, object["stt_mqtt_topic"] | "",
            sizeof(action.payload.stt.stt_mqtt_topic));
}

void stt_serialize(const ButtonAction& action, JsonObject object) {
    if (action.payload.stt.stt_command[0]) {
        object["stt_command"] = action.payload.stt.stt_command;
    }
    if (action.payload.stt.stt_mqtt_topic[0]) {
        object["stt_mqtt_topic"] = action.payload.stt.stt_mqtt_topic;
    }
}

void stt_dispatch(const ButtonAction& action, const char*) {
    const char* command = action.payload.stt.stt_command;
    if (strcmp(command, "record_start") == 0) {
        LOGI("STT", "Action record_start accepted=%d", stt_start_recording());
    } else if (strcmp(command, "record_stop_transcribe") == 0) {
        LOGI("STT", "Action record_stop_transcribe accepted=%d",
             stt_stop_and_transcribe(action.payload.stt.stt_mqtt_topic));
    } else {
        LOGW("STT", "Action rejected command=%s", command);
    }
}

void stt_describe(JsonObject& out) {
    out["group"] = "Audio";
    out["label"] = "Speech to text";
    out["command_field"] = "stt_command";
    JsonArray fields = out.createNestedArray("fields");
    JsonObject command = fields.createNestedObject();
    command["name"] = "stt_command";
    command["description"] = "record_start or record_stop_transcribe";
    JsonObject mqtt_topic = fields.createNestedObject();
    mqtt_topic["name"] = "stt_mqtt_topic";
    mqtt_topic["description"] = "optional MQTT topic to receive the successful transcript";
    JsonArray commands = out.createNestedArray("commands");
    JsonObject start = commands.createNestedObject();
    start["id"] = "record_start";
    start["label"] = "Start recording";
    JsonObject stop = commands.createNestedObject();
    stop["id"] = "record_stop_transcribe";
    stop["label"] = "Stop and transcribe";
}

const ActionTypeDef kSttActionType = {
    /* type_name   */ ACTION_TYPE_STT,
    /* parse       */ stt_parse,
    /* serialize   */ stt_serialize,
    /* dispatch    */ stt_dispatch,
    /* value_field */ nullptr,
    /* describe    */ stt_describe,
};

REGISTER_ACTION_TYPE(kSttActionType);

} // namespace

#endif // HAS_STT