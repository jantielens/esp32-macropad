// Exposure timer operations, aggregated by darkroom_timer_device_class.cpp.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_DARKROOM_TIMER

#include "darkroom_timer_payload.h"
#include "expose_timer.h"

#include <limits.h>
#include <stdio.h>

static void expose_describe(JsonObject& out) {
    out["group"] = "Darkroom Timer";
    out["label"] = "Exposure timer";
}

static const ActionArgumentDef expose_seconds[] = {
    {"seconds", "Seconds", ACTION_ARGUMENT_NUMBER, INT32_MIN, INT32_MAX, 0,
     true, "[event:step]", "number"},
};
static const ActionArgumentDef expose_stops[] = {
    {"stops", "F-stops", ACTION_ARGUMENT_NUMBER, INT32_MIN, INT32_MAX, 0,
     true, "[event:step]", "number"},
};
static const ActionArgumentDef expose_percent[] = {
    {"percent", "Dry-down (%)", ACTION_ARGUMENT_NUMBER, INT32_MIN, INT32_MAX, 0,
     true, "[event:step]", "number"},
};
#define EXPOSE_SIMPLE(fn, command) \
    static ActionResult fn(const ActionResolvedArgument*, const char*, uint32_t) { \
        expose_timer_dispatch(command, ""); return ACTION_COMPLETE; \
    }
#define EXPOSE_NUMBER(fn, command) \
    static ActionResult fn(const ActionResolvedArgument* args, const char*, uint32_t) { \
        char value[24]; snprintf(value, sizeof(value), "%.9g", static_cast<double>(args[0].number)); \
        expose_timer_dispatch(command, value); return ACTION_COMPLETE; \
    }
EXPOSE_SIMPLE(expose_toggle_op, "toggle")
EXPOSE_SIMPLE(expose_start_op, "start")
EXPOSE_SIMPLE(expose_stop_op, "stop")
EXPOSE_SIMPLE(expose_pause_op, "pause")
EXPOSE_SIMPLE(expose_resume_op, "resume")
EXPOSE_SIMPLE(expose_reset_op, "reset")
EXPOSE_SIMPLE(expose_focus_op, "focus")
EXPOSE_SIMPLE(expose_focus_off_op, "focus_off")
EXPOSE_SIMPLE(expose_focus_toggle_op, "focus_toggle")
EXPOSE_NUMBER(expose_set_time_op, "set_time")
EXPOSE_NUMBER(expose_adjust_seconds_op, "adjust_seconds")
EXPOSE_NUMBER(expose_adjust_stops_op, "adjust_stops")
EXPOSE_NUMBER(expose_set_dry_down_op, "set_dry_down")
EXPOSE_NUMBER(expose_adjust_dry_down_op, "adjust_dry_down")
#undef EXPOSE_SIMPLE
#undef EXPOSE_NUMBER
static const ActionOperationDef expose_operations[] = {
    {"exposure.toggle", "Toggle exposure", nullptr, 0, expose_toggle_op},
    {"exposure.start", "Start exposure", nullptr, 0, expose_start_op},
    {"exposure.stop", "Stop exposure", nullptr, 0, expose_stop_op},
    {"exposure.pause", "Pause exposure", nullptr, 0, expose_pause_op},
    {"exposure.resume", "Resume exposure", nullptr, 0, expose_resume_op},
    {"exposure.reset", "Reset exposure", nullptr, 0, expose_reset_op},
    {"exposure.focus.on", "Focus light on", nullptr, 0, expose_focus_op},
    {"exposure.focus.off", "Focus light off", nullptr, 0, expose_focus_off_op},
    {"exposure.focus.toggle", "Toggle focus light", nullptr, 0, expose_focus_toggle_op},
    {"exposure.time.set", "Set exposure time", expose_seconds, 1, expose_set_time_op},
    {"exposure.time.adjust", "Adjust exposure seconds", expose_seconds, 1, expose_adjust_seconds_op},
    {"exposure.stops.adjust", "Adjust exposure f-stops", expose_stops, 1, expose_adjust_stops_op},
    {"exposure.dry_down.set", "Set dry-down", expose_percent, 1, expose_set_dry_down_op},
    {"exposure.dry_down.adjust", "Adjust dry-down", expose_percent, 1, expose_adjust_dry_down_op},
};

DEFINE_AND_REGISTER_ACTION_TYPE(expose_action_type, ACTION_TYPE_EXPOSE, expose_describe, nullptr, ACTION_EXECUTION_SYNC,
    true, false, expose_operations, sizeof(expose_operations) / sizeof(expose_operations[0]));

#endif // HAS_DISPLAY && IS_DARKROOM_TIMER
