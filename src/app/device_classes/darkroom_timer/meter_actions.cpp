// Light meter operations, aggregated by darkroom_timer_device_class.cpp.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_DARKROOM_TIMER

#include "darkroom_timer_payload.h"
#include "meter.h"

#include <limits.h>
#include <stdio.h>

static void meter_describe(JsonObject& out) {
    out["group"] = "Darkroom Timer";
    out["label"] = "Light meter";
}

static const ActionArgumentDef meter_lux[] = {
    {"lux", "Lux", ACTION_ARGUMENT_NUMBER, INT32_MIN, INT32_MAX, 0,
     true, "[event:step]", "number"},
};
static const ActionArgumentDef meter_seconds[] = {
    {"seconds", "Seconds", ACTION_ARGUMENT_NUMBER, INT32_MIN, INT32_MAX, 0,
     true, "[event:step]", "number"},
};
#define METER_SIMPLE(fn, command) \
    static ActionResult fn(const ActionResolvedArgument*, const char*, uint32_t) { \
        meter_dispatch(command, ""); return ACTION_COMPLETE; \
    }
#define METER_NUMBER(fn, command) \
    static ActionResult fn(const ActionResolvedArgument* args, const char*, uint32_t) { \
        char value[24]; snprintf(value, sizeof(value), "%.9g", static_cast<double>(args[0].number)); \
        meter_dispatch(command, value); return ACTION_COMPLETE; \
    }
METER_SIMPLE(meter_read_lref_op, "read_lref")
METER_SIMPLE(meter_read_bright_op, "read_bright")
METER_SIMPLE(meter_read_dark_op, "read_dark")
METER_SIMPLE(meter_mag_a_op, "mag_measure_a")
METER_SIMPLE(meter_mag_b_op, "mag_measure_b")
METER_SIMPLE(meter_mag_clear_op, "mag_clear")
METER_NUMBER(meter_set_lref_op, "set_lref")
METER_NUMBER(meter_adjust_lref_op, "adjust_lref")
METER_NUMBER(meter_set_zone5_op, "set_zone5")
METER_NUMBER(meter_adjust_zone5_op, "adjust_zone5")
#undef METER_SIMPLE
#undef METER_NUMBER
static const ActionOperationDef meter_operations[] = {
    {"meter.reference.read", "Read Lref", nullptr, 0, meter_read_lref_op},
    {"meter.bright.read", "Read bright spot", nullptr, 0, meter_read_bright_op},
    {"meter.dark.read", "Read dark spot", nullptr, 0, meter_read_dark_op},
    {"meter.magnification.measure_a", "Measure magnification A", nullptr, 0, meter_mag_a_op},
    {"meter.magnification.measure_b", "Measure magnification B", nullptr, 0, meter_mag_b_op},
    {"meter.magnification.clear", "Clear magnification compensation", nullptr, 0, meter_mag_clear_op},
    {"meter.reference.set", "Set Lref", meter_lux, 1, meter_set_lref_op},
    {"meter.reference.adjust", "Adjust Lref", meter_lux, 1, meter_adjust_lref_op},
    {"meter.zone5.set", "Set Zone V time", meter_seconds, 1, meter_set_zone5_op},
    {"meter.zone5.adjust", "Adjust Zone V time", meter_seconds, 1, meter_adjust_zone5_op},
};

DEFINE_AND_REGISTER_ACTION_TYPE(meter_action_type, ACTION_TYPE_METER, meter_describe, nullptr, ACTION_EXECUTION_SYNC,
    true, false, meter_operations, sizeof(meter_operations) / sizeof(meter_operations[0]));

#endif // HAS_DISPLAY && IS_DARKROOM_TIMER
