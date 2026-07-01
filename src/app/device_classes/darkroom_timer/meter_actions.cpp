// Darkroom-timer ActionTypeDef — parse, serialize, dispatch for the "meter"
// (light metering) action type. Registered via REGISTER_ACTION_TYPE so
// action_dispatch.cpp / action_parse.cpp do not need to know about the meter
// arm at compile time.
//
// Aggregated into the build via
// device_classes/darkroom_timer/darkroom_timer_device_class.cpp
// under #if IS_DARKROOM_TIMER.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_DARKROOM_TIMER

#include "../../log_manager.h"
#include "darkroom_timer_payload.h"
#include "meter.h"

#include <string.h>

#define TAG "MeterAction"

// Wire format: flat JSON {meter_command, meter_value} (legacy field names
// preserved for field-deployed pad-config backward compatibility).
static void meter_parse(const JsonObject& a, ButtonAction& act) {
    MeterPayload& p = meter_payload(act);
    strlcpy(p.command, a["meter_command"] | "", sizeof(p.command));
    strlcpy(p.value,   a["meter_value"]   | "", sizeof(p.value));
}

static void meter_serialize(const ButtonAction& act, JsonObject obj) {
    const MeterPayload& p = meter_payload(act);
    if (p.command[0]) obj["meter_command"] = p.command;
    if (p.value[0])   obj["meter_value"]   = p.value;
}

static void meter_action_dispatch(const ButtonAction& act, const char* label) {
    const MeterPayload& p = meter_payload(act);
    if (!p.command[0]) {
        LOGW(TAG, "%s meter: empty command", label);
        return;
    }
    LOGI(TAG, "%s meter: %s %s", label, p.command, p.value);
    meter_dispatch(p.command, p.value);
}

// `value` is the single bindable/numeric field (numeric rocker {step} target
// for adjust_* commands); expose it so shared code drives binding + {step}.
static char* meter_value_field(ButtonAction& act, size_t* out_size) {
    MeterPayload& p = meter_payload(act);
    *out_size = sizeof(p.value);
    return p.value;
}

static const ActionTypeDef meter_action_type = {
    /* type_name   */ ACTION_TYPE_METER,
    /* parse       */ meter_parse,
    /* serialize   */ meter_serialize,
    /* dispatch    */ meter_action_dispatch,
    /* value_field */ meter_value_field,
};

REGISTER_ACTION_TYPE(meter_action_type);

#endif // HAS_DISPLAY && IS_DARKROOM_TIMER
