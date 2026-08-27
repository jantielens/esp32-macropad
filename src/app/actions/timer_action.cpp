#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_DISPLAY
#include "timer_command.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kTimerActionTag = "Action";
void parse_timer(const JsonObject& action, ButtonAction& act) { const char* command = action["timer_command"] | ""; const char* mode = action["timer_mode"] | ""; const char* value = action["timer_value"] | ""; if (strlen(command) >= sizeof(act.payload.timer.timer_command) || strlen(mode) >= sizeof(act.payload.timer.timer_mode) || strlen(value) >= sizeof(act.payload.timer.timer_value)) { memset(&act, 0, sizeof(act)); return; } act.payload.timer.timer_id = action["timer_id"] | 0; strlcpy(act.payload.timer.timer_command, command, sizeof(act.payload.timer.timer_command)); strlcpy(act.payload.timer.timer_mode, mode, sizeof(act.payload.timer.timer_mode)); strlcpy(act.payload.timer.timer_value, value, sizeof(act.payload.timer.timer_value)); }
void serialize_timer(const ButtonAction& act, JsonObject action) { if (act.payload.timer.timer_id) action["timer_id"] = act.payload.timer.timer_id; if (act.payload.timer.timer_command[0]) action["timer_command"] = act.payload.timer.timer_command; if (act.payload.timer.timer_mode[0]) action["timer_mode"] = act.payload.timer.timer_mode; if (act.payload.timer.timer_value[0]) action["timer_value"] = act.payload.timer.timer_value; }
ActionResult dispatch_timer(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_DISPLAY
    char error[96]; if (timer_command_run(act.payload.timer, error, sizeof(error))) LOGI(kTimerActionTag, "%s timer: %u:%s", label, act.payload.timer.timer_id, act.payload.timer.timer_command); else LOGW(kTimerActionTag, "%s timer: %s", label, error);
#else
    (void)act;
    LOGW(kTimerActionTag, "%s timer: no display", label);
#endif
    return ACTION_COMPLETE;
}
bool timer_available() { return HAS_DISPLAY; }
const char* validate_timer(const JsonObjectConst action) { if (action.containsKey("timer_id") && !action["timer_id"].is<uint8_t>()) return "timer_id must be a whole number"; if (action.containsKey("timer_command") && !action["timer_command"].is<const char*>()) return "timer_command must be a string"; return action.containsKey("timer_value") && !action["timer_value"].is<const char*>() ? "timer_value must be a string" : nullptr; }
bool visit_timer_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) { return !act.payload.timer.timer_value[0] || visitor(act.payload.timer.timer_value, sizeof(act.payload.timer.timer_value), true, context); }
void describe_timer(JsonObject& action) {
    action["group"] = "Timer";
    action["label"] = "Timer";
    JsonArray commands = action.createNestedArray("commands");
    const char* const command_ids[] = {
        "toggle", "start", "stop", "pause", "resume", "reset", "set", "adjust"
    };
    const char* const command_labels[] = {
        "Toggle", "Start", "Stop", "Pause", "Resume", "Reset", "Set countdown", "Adjust countdown"
    };
    for (size_t i = 0; i < sizeof(command_ids) / sizeof(command_ids[0]); ++i) {
        JsonObject command = commands.createNestedObject();
        command["id"] = command_ids[i];
        command["label"] = command_labels[i];
    }
    JsonArray fields = action.createNestedArray("fields");
    JsonObject id = fields.createNestedObject();
    id["name"] = "timer_id";
    id["description"] = "1-3";
    JsonObject value = fields.createNestedObject();
    value["name"] = "timer_value";
    value["description"] = "countdown value, bindable";
}
DEFINE_AND_REGISTER_ACTION_TYPE(kTimerActionType, ACTION_TYPE_TIMER, parse_timer, serialize_timer, dispatch_timer, nullptr, describe_timer, timer_available, validate_timer, visit_timer_fields);
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON