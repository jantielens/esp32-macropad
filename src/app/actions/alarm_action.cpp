#include "action_registry.h"
#if ALARM_ENABLED
#include "alarm_manager.h"
#include <limits.h>
namespace {
const char* const kAlarmTargets[] = {"0", "1"};
const char* const kAlarmTargetLabels[] = {"Active alarm", "Alarm 1"};
const char* const kAlarmDays[] = {"0", "1", "2", "3", "4", "5", "6"};
const char* const kAlarmDayLabels[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const ActionArgumentDef kAlarmSessionArguments[] = {
    {"alarm", "Alarm", ACTION_ARGUMENT_ENUM, 0, 0, 1, true, "[var:alarm]", "select",
     kAlarmTargets, 2, false, nullptr, kAlarmTargetLabels},
};
const ActionArgumentDef kAlarmTimeArguments[] = {
    {"minutes", "Minutes since midnight", ACTION_ARGUMENT_INTEGER, 0, 1439, 0,
     true, "[var:minutes]", "number"},
};
const ActionArgumentDef kAlarmAdjustArguments[] = {
    {"minutes", "Adjust (minutes)", ACTION_ARGUMENT_INTEGER, INT32_MIN, INT32_MAX, 0,
     true, "[event:step]", "number"},
};
const ActionArgumentDef kAlarmWeekdayArguments[] = {
    {"day", "Weekday", ACTION_ARGUMENT_ENUM, 0, 0, 1, true, "[var:weekday]", "select",
     kAlarmDays, 7, false, nullptr, kAlarmDayLabels},
};

ActionResult run_alarm_operation(const char* command, uint8_t id, int value, uint8_t day) {
    return alarm_command_submit(command, id, value, day) ? ACTION_COMPLETE : ACTION_FAILED;
}
#define ALARM_SESSION_OPERATION(name, command) \
    ActionResult name(const ActionResolvedArgument* args, const char*, uint32_t) { \
        return run_alarm_operation(command, static_cast<uint8_t>(args[0].text[0] - '0'), 0, 0); \
    }
#define ALARM_VALUE_OPERATION(name, command) \
    ActionResult name(const ActionResolvedArgument* args, const char*, uint32_t) { \
        return run_alarm_operation(command, 1, args[0].integer, 0); \
    }
#define ALARM_SIMPLE_OPERATION(name, command) \
    ActionResult name(const ActionResolvedArgument*, const char*, uint32_t) { \
        return run_alarm_operation(command, 1, 0, 0); \
    }
#define ALARM_DAY_OPERATION(name, command) \
    ActionResult name(const ActionResolvedArgument* args, const char*, uint32_t) { \
        return run_alarm_operation(command, 1, 0, static_cast<uint8_t>(args[0].text[0] - '0')); \
    }
ALARM_SESSION_OPERATION(run_alarm_cancel, "cancel")
ALARM_SESSION_OPERATION(run_alarm_snooze, "snooze")
ALARM_VALUE_OPERATION(run_alarm_set_time, "set_time")
ALARM_VALUE_OPERATION(run_alarm_adjust, "adjust_minutes")
ALARM_SIMPLE_OPERATION(run_alarm_enable, "enable")
ALARM_SIMPLE_OPERATION(run_alarm_disable, "disable")
ALARM_SIMPLE_OPERATION(run_alarm_toggle, "toggle")
ALARM_DAY_OPERATION(run_alarm_weekday_enable, "weekday_enable")
ALARM_DAY_OPERATION(run_alarm_weekday_disable, "weekday_disable")
ALARM_DAY_OPERATION(run_alarm_weekday_toggle, "weekday_toggle")
#undef ALARM_SESSION_OPERATION
#undef ALARM_VALUE_OPERATION
#undef ALARM_SIMPLE_OPERATION
#undef ALARM_DAY_OPERATION

const ActionOperationDef kAlarmOperations[] = {
    {"alarm.cancel", "Dismiss alarm", kAlarmSessionArguments, 1, run_alarm_cancel},
    {"alarm.snooze", "Snooze alarm", kAlarmSessionArguments, 1, run_alarm_snooze},
    {"alarm.set_time", "Set alarm time", kAlarmTimeArguments, 1, run_alarm_set_time},
    {"alarm.adjust_minutes", "Adjust alarm time", kAlarmAdjustArguments, 1, run_alarm_adjust},
    {"alarm.enable", "Enable alarm", nullptr, 0, run_alarm_enable},
    {"alarm.disable", "Disable alarm", nullptr, 0, run_alarm_disable},
    {"alarm.toggle", "Toggle alarm", nullptr, 0, run_alarm_toggle},
    {"alarm.weekday_enable", "Enable alarm weekday", kAlarmWeekdayArguments, 1, run_alarm_weekday_enable},
    {"alarm.weekday_disable", "Disable alarm weekday", kAlarmWeekdayArguments, 1, run_alarm_weekday_disable},
    {"alarm.weekday_toggle", "Toggle alarm weekday", kAlarmWeekdayArguments, 1, run_alarm_weekday_toggle},
};

void describe_alarm(JsonObject& action) {
    action["group"] = "Alarm";
    action["label"] = "Alarm Control";
}
DEFINE_AND_REGISTER_ACTION_TYPE(kAlarmActionType, ACTION_TYPE_ALARM, describe_alarm, nullptr, ACTION_EXECUTION_SYNC, false, false,
    kAlarmOperations, sizeof(kAlarmOperations) / sizeof(kAlarmOperations[0]));
}
#endif