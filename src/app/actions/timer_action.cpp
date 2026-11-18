#include "action_registry.h"
#include "log_manager.h"
#include "timer_engine.h"
#include <limits.h>
#include <stdio.h>
#if defined(ARDUINO) && HAS_DISPLAY
#include "timer_command.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kTimerActionTag = "Action";
const ActionArgumentDef kAdjustTimerArguments[] = {
    {"timer", "Timer", ACTION_ARGUMENT_INTEGER, 1, TIMER_COUNT, 0, true, "[var:timer]", "timers"},
    {"seconds", "Adjust (seconds)", ACTION_ARGUMENT_INTEGER, INT32_MIN, INT32_MAX, 0,
     true, "[expr:[event:step]]", "number"},
};
const ActionArgumentDef kTimerArguments[] = {
    {"timer", "Timer", ACTION_ARGUMENT_INTEGER, 1, TIMER_COUNT, 0, true, "[var:timer]", "timers"},
};
const ActionArgumentDef kCountdownArguments[] = {
    {"timer", "Timer", ACTION_ARGUMENT_INTEGER, 1, TIMER_COUNT, 0, true, "[var:timer]", "timers"},
    {"seconds", "Countdown (seconds)", ACTION_ARGUMENT_INTEGER, 1, UINT32_MAX / 1000, 0,
     true, "[var:seconds]", "number"},
};
const ActionArgumentDef kSetTimerArguments[] = {
    {"timer", "Timer", ACTION_ARGUMENT_INTEGER, 1, TIMER_COUNT, 0, true, "[var:timer]", "timers"},
    {"seconds", "Countdown (seconds)", ACTION_ARGUMENT_INTEGER, 0, UINT32_MAX / 1000, 0,
     true, "[var:seconds]", "number"},
};

ActionResult run_timer(const ActionResolvedArgument* arguments, const char* label,
                       const char* command, const char* mode, bool has_seconds) {
#if defined(ARDUINO) && HAS_DISPLAY
    TimerPayload payload = {};
    payload.timer_id = static_cast<uint8_t>(arguments[0].integer);
    strlcpy(payload.timer_command, command, sizeof(payload.timer_command));
    if (mode) strlcpy(payload.timer_mode, mode, sizeof(payload.timer_mode));
    if (has_seconds)
        snprintf(payload.timer_value, sizeof(payload.timer_value), "%ld",
                 static_cast<long>(arguments[1].integer));
    char error[96] = {};
    if (timer_command_run(payload, error, sizeof(error))) return ACTION_COMPLETE;
    LOGW(kTimerActionTag, "%s timer: %s", label, error);
    return ACTION_FAILED;
#else
    (void)arguments;
    (void)command;
    (void)mode;
    (void)has_seconds;
    LOGW(kTimerActionTag, "%s timer: no display", label);
    return ACTION_FAILED;
#endif
}

#define TIMER_OPERATION(name, command, mode, has_seconds) \
    ActionResult name(const ActionResolvedArgument* arguments, const char* label, uint32_t) { \
        return run_timer(arguments, label, command, mode, has_seconds); \
    }
TIMER_OPERATION(dispatch_adjust_timer, "adjust", nullptr, true)
TIMER_OPERATION(dispatch_set_timer, "set", nullptr, true)
TIMER_OPERATION(dispatch_start_up, "start", "up", false)
TIMER_OPERATION(dispatch_start_down, "start", "down", true)
TIMER_OPERATION(dispatch_toggle_up, "toggle", "up", false)
TIMER_OPERATION(dispatch_toggle_down, "toggle", "down", true)
TIMER_OPERATION(dispatch_stop_timer, "stop", nullptr, false)
TIMER_OPERATION(dispatch_pause_timer, "pause", nullptr, false)
TIMER_OPERATION(dispatch_resume_timer, "resume", nullptr, false)
TIMER_OPERATION(dispatch_reset_timer, "reset", nullptr, false)
#undef TIMER_OPERATION

const ActionOperationDef kTimerOperations[] = {
    {"timer.adjust", "Adjust timer", kAdjustTimerArguments, 2, dispatch_adjust_timer},
    {"timer.set", "Set timer countdown", kSetTimerArguments, 2, dispatch_set_timer},
    {"timer.start_up", "Start count-up timer", kTimerArguments, 1, dispatch_start_up},
    {"timer.start_down", "Start countdown timer", kCountdownArguments, 2, dispatch_start_down},
    {"timer.toggle_up", "Toggle count-up timer", kTimerArguments, 1, dispatch_toggle_up},
    {"timer.toggle_down", "Toggle countdown timer", kCountdownArguments, 2, dispatch_toggle_down},
    {"timer.stop", "Stop timer", kTimerArguments, 1, dispatch_stop_timer},
    {"timer.pause", "Pause timer", kTimerArguments, 1, dispatch_pause_timer},
    {"timer.resume", "Resume timer", kTimerArguments, 1, dispatch_resume_timer},
    {"timer.reset", "Reset timer", kTimerArguments, 1, dispatch_reset_timer},
};
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
bool visit_timer_fields(ButtonAction& act, ActionFieldVisitor visitor, void* context) {
    return action_visit_field(visitor, context, "timer_value", act.payload.timer.timer_value);
}
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
DEFINE_AND_REGISTER_ACTION_TYPE(kTimerActionType, ACTION_TYPE_TIMER, parse_timer, serialize_timer, dispatch_timer, visit_timer_fields, describe_timer, timer_available, validate_timer, ACTION_EXECUTION_SYNC, true, true, false, nullptr, kTimerOperations, sizeof(kTimerOperations) / sizeof(kTimerOperations[0]));
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON