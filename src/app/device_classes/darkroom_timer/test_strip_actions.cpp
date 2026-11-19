// Test strip operations, aggregated by darkroom_timer_device_class.cpp.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_DARKROOM_TIMER

#include "darkroom_timer_payload.h"
#include "test_strip.h"

#include <limits.h>
#include <stdio.h>

static void strip_describe(JsonObject& out) {
    out["group"] = "Darkroom Timer";
    out["label"] = "Test strip";
}

static const ActionArgumentDef strip_seconds[] = {
    {"seconds", "Base time (seconds)", ACTION_ARGUMENT_NUMBER, INT32_MIN, INT32_MAX, 0,
     true, "[event:step]", "number"},
};
static const ActionArgumentDef strip_segments[] = {
    {"segments", "Segments", ACTION_ARGUMENT_INTEGER, INT32_MIN, INT32_MAX, 0,
     true, "[event:step]", "number"},
};
static const ActionArgumentDef strip_duration[] = {
    {"seconds", "Seconds", ACTION_ARGUMENT_INTEGER, INT32_MIN, INT32_MAX, 0,
     true, "[event:step]", "number"},
};
static const ActionArgumentDef strip_tick[] = {
    {"enabled", "Exposure tick", ACTION_ARGUMENT_BOOLEAN, 0, 0, 0,
     true, "[var:tick_enabled]", "toggle"},
};
#define STRIP_SIMPLE(fn, command) \
    static ActionResult fn(const ActionResolvedArgument*, const char*, uint32_t) { \
        test_strip_dispatch(command, ""); return ACTION_COMPLETE; \
    }
#define STRIP_NUMBER(fn, command) \
    static ActionResult fn(const ActionResolvedArgument* args, const char*, uint32_t) { \
        char value[24]; snprintf(value, sizeof(value), "%.9g", static_cast<double>(args[0].number)); \
        test_strip_dispatch(command, value); return ACTION_COMPLETE; \
    }
#define STRIP_INTEGER(fn, command) \
    static ActionResult fn(const ActionResolvedArgument* args, const char*, uint32_t) { \
        int32_t n = args[0].integer; \
        /* The strip adds adjustments to state values no larger than 15. */ \
        if (n > INT32_MAX - 15) n = INT32_MAX - 15; \
        char value[16]; snprintf(value, sizeof(value), "%ld", static_cast<long>(n)); \
        test_strip_dispatch(command, value); return ACTION_COMPLETE; \
    }
STRIP_SIMPLE(strip_start_op, "start")
STRIP_SIMPLE(strip_cancel_op, "cancel")
STRIP_SIMPLE(strip_step_up_op, "step_up")
STRIP_SIMPLE(strip_step_down_op, "step_down")
STRIP_NUMBER(strip_set_base_op, "set_base")
STRIP_NUMBER(strip_adjust_base_op, "adjust_base")
STRIP_INTEGER(strip_adjust_segments_op, "adjust_segments")
STRIP_INTEGER(strip_set_segments_op, "set_segments")
STRIP_INTEGER(strip_set_countdown_op, "set_countdown")
STRIP_INTEGER(strip_adjust_countdown_op, "adjust_countdown")
STRIP_INTEGER(strip_set_pause_op, "set_pause")
STRIP_INTEGER(strip_adjust_pause_op, "adjust_pause")
#undef STRIP_SIMPLE
#undef STRIP_NUMBER
#undef STRIP_INTEGER
static ActionResult strip_set_tick_op(const ActionResolvedArgument* args, const char*, uint32_t) {
    test_strip_dispatch("set_tick", args[0].integer ? "on" : "off");
    return ACTION_COMPLETE;
}
static const ActionOperationDef strip_operations[] = {
    {"strip.start", "Start test strip", nullptr, 0, strip_start_op},
    {"strip.cancel", "Cancel test strip", nullptr, 0, strip_cancel_op},
    {"strip.step.increase", "Increase step interval", nullptr, 0, strip_step_up_op},
    {"strip.step.decrease", "Decrease step interval", nullptr, 0, strip_step_down_op},
    {"strip.base.set", "Set base time", strip_seconds, 1, strip_set_base_op},
    {"strip.base.adjust", "Adjust base time", strip_seconds, 1, strip_adjust_base_op},
    {"strip.segments.adjust", "Adjust segments", strip_segments, 1, strip_adjust_segments_op},
    {"strip.segments.set", "Set segments", strip_segments, 1, strip_set_segments_op},
    {"strip.countdown.set", "Set initial countdown", strip_duration, 1, strip_set_countdown_op},
    {"strip.countdown.adjust", "Adjust initial countdown", strip_duration, 1, strip_adjust_countdown_op},
    {"strip.pause.set", "Set pause duration", strip_duration, 1, strip_set_pause_op},
    {"strip.pause.adjust", "Adjust pause duration", strip_duration, 1, strip_adjust_pause_op},
    {"strip.tick.set", "Set exposure tick", strip_tick, 1, strip_set_tick_op},
};

DEFINE_AND_REGISTER_ACTION_TYPE(strip_action_type, ACTION_TYPE_STRIP, strip_describe, nullptr, ACTION_EXECUTION_SYNC,
    true, false, strip_operations, sizeof(strip_operations) / sizeof(strip_operations[0]));

#endif // HAS_DISPLAY && IS_DARKROOM_TIMER
