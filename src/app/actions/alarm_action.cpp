#include "action_registry.h"
#if ALARM_ENABLED
#include "alarm_manager.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
namespace {
bool alarm_integer(const char* text, int* output) {
    if (!text || !text[0] || (text[0] != '+' && text[0] != '-' && (text[0] < '0' || text[0] > '9'))) return false;
    errno = 0;
    char* end = nullptr;
    const long value = strtol(text, &end, 10);
    if (errno || end == text || *end || value < INT_MIN || value > INT_MAX) return false;
    *output = static_cast<int>(value);
    return true;
}
const char* validate_alarm(JsonObjectConst action) {
    if (action.containsKey("alarm_id") && (!action["alarm_id"].is<uint8_t>() || action["alarm_id"].as<uint8_t>() > 1))
        return "alarm_id must be 0 (active) or 1";
    const char* command = action["alarm_command"] | "";
    const uint8_t operation = alarm_command_operation(command);
    const uint8_t id = action["alarm_id"] | (operation > 2 ? 1 : 0);
    if (action.containsKey("alarm_day") && (!action["alarm_day"].is<uint8_t>() || action["alarm_day"].as<uint8_t>() > 6))
        return "alarm_day must be 0 (Sunday) through 6 (Saturday)";
    if (operation >= 8 && !action.containsKey("alarm_day")) return "Weekday commands require alarm_day";
    int value = 0;
    if (action.containsKey("alarm_value") && (!action["alarm_value"].is<const char*>()
        || strlen(action["alarm_value"].as<const char*>()) >= CONFIG_VALUE_MAX_LEN)) return "alarm_value must be a bounded string";
    if (operation == 3 || operation == 4) {
        const char* text = action["alarm_value"] | "";
        if (!text[0]) return "Time commands require alarm_value";
        if (!strchr(text, '[') && !strstr(text, "{step}") && !alarm_integer(text, &value)) return "alarm_value must be whole minutes or a binding/{step}";
    }
    return alarm_command_validate(command, id, value, action["alarm_day"] | 0);
}
void parse_alarm(const JsonObject& action, ButtonAction& output) {
    if (validate_alarm(action)) { memset(&output, 0, sizeof(output)); return; }
    output.payload.alarm.alarm_id = action["alarm_id"] | (alarm_command_operation(action["alarm_command"] | "") > 2 ? 1 : 0);
    strlcpy(output.payload.alarm.alarm_command, action["alarm_command"], sizeof(output.payload.alarm.alarm_command));
    output.payload.alarm.alarm_day = action["alarm_day"] | 0;
    strlcpy(output.payload.alarm.alarm_value, action["alarm_value"] | "", sizeof(output.payload.alarm.alarm_value));
}
void serialize_alarm(const ButtonAction& input, JsonObject action) {
    action["alarm_id"] = input.payload.alarm.alarm_id;
    action["alarm_command"] = input.payload.alarm.alarm_command;
    if (alarm_command_operation(input.payload.alarm.alarm_command) >= 8) action["alarm_day"] = input.payload.alarm.alarm_day;
    if (input.payload.alarm.alarm_value[0]) action["alarm_value"] = input.payload.alarm.alarm_value;
}
ActionResult dispatch_alarm(const ButtonAction& action, const char*, uint32_t) {
    const auto& payload = action.payload.alarm;
    int value = 0;
    const uint8_t operation = alarm_command_operation(payload.alarm_command);
    if ((operation == 3 || operation == 4) && !alarm_integer(payload.alarm_value, &value)) {
        alarm_command_report_error("Resolved alarm_value must be signed 32-bit whole minutes");
        return ACTION_FAILED;
    }
    return alarm_command_submit(payload.alarm_command, payload.alarm_id, value, payload.alarm_day) ? ACTION_COMPLETE : ACTION_FAILED;
}
char* alarm_value_field(ButtonAction& action, size_t* size) {
    const uint8_t operation = alarm_command_operation(action.payload.alarm.alarm_command);
    if (operation != 3 && operation != 4) return nullptr;
    *size = sizeof(action.payload.alarm.alarm_value);
    return action.payload.alarm.alarm_value;
}
void describe_alarm(JsonObject& action) {
    action["group"] = "Alarm";
    action["label"] = "Alarm Control";
    JsonArray commands = action.createNestedArray("commands");
    const char* labels[] = {"", "Cancel", "Snooze", "Set time", "Adjust time", "Enable alarm", "Disable alarm", "Toggle alarm", "Enable weekday", "Disable weekday", "Toggle weekday"};
    for (uint8_t index = 1; index < sizeof(alarm_command_names) / sizeof(alarm_command_names[0]); ++index) {
        JsonObject item = commands.createNestedObject();
        item["id"] = alarm_command_names[index];
        item["label"] = labels[index];
    }
    JsonArray fields = action.createNestedArray("fields");
    JsonObject id = fields.createNestedObject();
    id["name"] = "alarm_id";
    id["description"] = "0 targets the active session for cancel/snooze only; configuration requires 1; omitted defaults to 1 for configuration, 0 for session commands";
    JsonObject command = fields.createNestedObject();
    command["name"] = "alarm_command";
    command["description"] = "cancel, snooze, set_time, adjust_minutes, enable, disable, toggle, weekday_enable, weekday_disable, weekday_toggle";
    JsonObject value = fields.createNestedObject();
    value["name"] = "alarm_value";
    value["description"] = "set_time: whole minutes since midnight (0-1439); adjust_minutes: signed 32-bit minutes, wraps within 24 hours; string supports bindings and {step}";
    JsonObject day = fields.createNestedObject();
    day["name"] = "alarm_day";
    day["description"] = "required for weekday commands: Sunday=0 through Saturday=6";
    JsonArray editor = action.createNestedArray("editor_fields");
    JsonObject target = editor.createNestedObject();
    target["name"] = "alarm_id";
    target["label"] = "Alarm";
    target["type"] = "select";
    target["default"] = "1";
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
    JsonObject editor_value = editor.createNestedObject();
    editor_value["name"] = "alarm_value";
    editor_value["label"] = "Minutes (set time: since midnight; adjust: signed step)";
    editor_value["type"] = "text";
    editor_value["bindable"] = true;
    JsonObject editor_day = editor.createNestedObject();
    editor_day["name"] = "alarm_day";
    editor_day["label"] = "Weekday (weekday commands only)";
    editor_day["type"] = "select";
    editor_day["numeric"] = true;
    editor_day["default"] = "1";
    JsonArray days = editor_day.createNestedArray("options");
    const char* names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    for (uint8_t index = 0; index < 7; ++index) {
        JsonObject item = days.createNestedObject();
        item["id"] = index;
        item["label"] = names[index];
    }
}
DEFINE_AND_REGISTER_ACTION_TYPE(kAlarmActionType, ACTION_TYPE_ALARM, parse_alarm, serialize_alarm, dispatch_alarm, alarm_value_field, describe_alarm, nullptr, validate_alarm, nullptr, ACTION_EXECUTION_SYNC, false);
}
#endif