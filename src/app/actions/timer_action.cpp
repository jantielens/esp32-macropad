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
bool timer_available() { return HAS_DISPLAY; }
void describe_timer(JsonObject& action) {
    action["group"] = "Timer";
    action["label"] = "Timer";
}
DEFINE_AND_REGISTER_ACTION_TYPE(kTimerActionType, ACTION_TYPE_TIMER, describe_timer, timer_available, ACTION_EXECUTION_SYNC, true, false, kTimerOperations, sizeof(kTimerOperations) / sizeof(kTimerOperations[0]));
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON