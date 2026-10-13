#include "heart_recorder.h"

#if IS_HEART_MONITOR
#include "action_registry.h"
#include <string.h>

namespace heart_monitor {
namespace {

constexpr char action_type[] = "heart";
struct Payload { char command[16]; };
static_assert(sizeof(Payload) <= ACTION_PAYLOAD_DEVICE_CLASS_BYTES, "Heart payload exceeds device-class storage");

Payload& payload(ButtonAction& action) {
    return *reinterpret_cast<Payload*>(action.payload.device_class);
}
const Payload& payload(const ButtonAction& action) {
    return *reinterpret_cast<const Payload*>(action.payload.device_class);
}

bool parse_command(const char* name, Command& command) {
    if (!name) return false;
    if (strcmp(name, "start") == 0) command = Command::Start;
    else if (strcmp(name, "stop") == 0) command = Command::Stop;
    else if (strcmp(name, "retry_save") == 0) command = Command::RetrySave;
    else if (strcmp(name, "discard") == 0) command = Command::Discard;
    else return false;
    return true;
}

void parse(const JsonObject& object, ButtonAction& action) {
    strlcpy(payload(action).command, object["command"] | "", sizeof(payload(action).command));
}
void serialize(const ButtonAction& action, JsonObject object) {
    object["command"] = payload(action).command;
}
const char* validate(JsonObjectConst object) {
    Command command;
    return parse_command(object["command"] | "", command) ? nullptr : "heart requires start, stop, retry_save, or discard";
}
ActionResult dispatch(const ButtonAction& action, const char*, uint32_t) {
    Command command;
    return parse_command(payload(action).command, command) && heart_recorder_command(command) ? ACTION_COMPLETE : ACTION_FAILED;
}
void heart_describe(JsonObject& out) {
    out["group"] = "Heart Monitor";
    out["label"] = "Raw recording";
    out["command_field"] = "command";
    JsonArray commands = out.createNestedArray("commands");
    const char* names[] = {"start", "stop", "retry_save", "discard"};
    const char* labels[] = {"Start recording", "Stop and save", "Retry save", "Discard unsaved recording"};
    for (size_t index = 0; index < 4; ++index) {
        JsonObject item = commands.createNestedObject();
        item["id"] = names[index];
        item["label"] = labels[index];
    }
    JsonArray fields = out.createNestedArray("fields");
    JsonObject field = fields.createNestedObject();
    field["name"] = "command";
    field["description"] = "Local raw capture control; saving failures retain samples until retry or discard";
    JsonArray editor = out.createNestedArray("editor_fields");
    JsonObject command = editor.createNestedObject();
    command["name"] = "command";
    command["label"] = "Command";
    command["type"] = "select";
    command["command_options"] = true;
    command["default"] = "start";
}

DEFINE_AND_REGISTER_ACTION_TYPE(heart_action, action_type, parse, serialize, dispatch,
    nullptr, heart_describe, nullptr, validate, nullptr, ACTION_EXECUTION_SYNC, false);

}
}
#endif