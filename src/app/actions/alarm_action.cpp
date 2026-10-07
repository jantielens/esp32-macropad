#include "action_registry.h"
#if ALARM_ENABLED
#include "alarm_manager.h"
namespace {
const char* validate_alarm(JsonObjectConst action) {
    if (action.containsKey("alarm_id") && (!action["alarm_id"].is<uint8_t>() || action["alarm_id"].as<uint8_t>() > 1))
        return "alarm_id must be 0 (active) or 1";
    const char* command = action["alarm_command"] | "";
    return !strcmp(command, "cancel") || !strcmp(command, "snooze") ? nullptr : "alarm_command must be cancel or snooze";
}
void parse_alarm(const JsonObject& action, ButtonAction& output) {
    if (validate_alarm(action)) { memset(&output, 0, sizeof(output)); return; }
    output.payload.alarm.alarm_id = action["alarm_id"] | 0;
    strlcpy(output.payload.alarm.alarm_command, action["alarm_command"], sizeof(output.payload.alarm.alarm_command));
}
void serialize_alarm(const ButtonAction& input, JsonObject action) {
    action["alarm_id"] = input.payload.alarm.alarm_id;
    action["alarm_command"] = input.payload.alarm.alarm_command;
}
ActionResult dispatch_alarm(const ButtonAction& action, const char*, uint32_t) {
    return alarm_command_submit(action.payload.alarm.alarm_command, action.payload.alarm.alarm_id) ? ACTION_COMPLETE : ACTION_FAILED;
}
void describe_alarm(JsonObject& action) {
    action["group"] = "Alarm";
    action["label"] = "Alarm Control";
    JsonArray commands = action.createNestedArray("commands");
    for (const char* command : {"snooze", "cancel"}) {
        JsonObject item = commands.createNestedObject();
        item["id"] = command;
        item["label"] = !strcmp(command, "snooze") ? "Snooze" : "Cancel";
    }
    JsonArray fields = action.createNestedArray("fields");
    JsonObject id = fields.createNestedObject();
    id["name"] = "alarm_id";
    id["description"] = "0 targets the active alarm; 1 targets slot 1";
    JsonObject command = fields.createNestedObject();
    command["name"] = "alarm_command";
    command["description"] = "snooze or cancel";
    JsonArray editor = action.createNestedArray("editor_fields");
    JsonObject target = editor.createNestedObject();
    target["name"] = "alarm_id";
    target["label"] = "Alarm";
    target["type"] = "select";
    target["default"] = "0";
    target["numeric"] = true;
    JsonArray options = target.createNestedArray("options");
    JsonObject active = options.createNestedObject();
    active["id"] = "0";
    active["label"] = "Active alarm";
    JsonObject slot = options.createNestedObject();
    slot["id"] = "1";
    slot["label"] = "Alarm 1";
    JsonObject operation = editor.createNestedObject();
    operation["name"] = "alarm_command";
    operation["label"] = "Command";
    operation["type"] = "select";
    operation["default"] = "snooze";
    operation["command_options"] = true;
}
DEFINE_AND_REGISTER_ACTION_TYPE(kAlarmActionType, ACTION_TYPE_ALARM, parse_alarm, serialize_alarm, dispatch_alarm, nullptr, describe_alarm, nullptr, validate_alarm, nullptr, ACTION_EXECUTION_SYNC);
}
#endif