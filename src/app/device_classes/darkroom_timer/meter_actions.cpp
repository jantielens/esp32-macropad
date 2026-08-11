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

static ActionResult meter_action_dispatch(const ButtonAction& act, const char* label,
                                          uint32_t /*continuation_token*/) {
    const MeterPayload& p = meter_payload(act);
    if (!p.command[0]) {
        LOGW(TAG, "%s meter: empty command", label);
        return ACTION_COMPLETE;
    }
    LOGI(TAG, "%s meter: %s %s", label, p.command, p.value);
    meter_dispatch(p.command, p.value);
    return ACTION_COMPLETE;
}

// `value` is the single bindable/numeric field (numeric rocker {step} target
// for adjust_* commands); expose it so shared code drives binding + {step}.
static char* meter_value_field(ButtonAction& act, size_t* out_size) {
    MeterPayload& p = meter_payload(act);
    *out_size = sizeof(p.value);
    return p.value;
}

static void meter_describe(JsonObject& out) {
    out["group"] = "Darkroom Timer";
    out["label"] = "Light meter";
    JsonArray fields = out.createNestedArray("fields");
    JsonObject command_field = fields.createNestedObject();
    command_field["name"] = "meter_command";
    command_field["description"] = "required command identifier";
    JsonObject value_field = fields.createNestedObject();
    value_field["name"] = "meter_value";
    value_field["description"] = "lux or seconds for set/adjust commands; binding templates are supported";
    JsonArray commands = out.createNestedArray("commands");
    const char* ids[] = {"read_lref", "read_bright", "read_dark", "set_lref", "adjust_lref", "set_zone5", "adjust_zone5", "mag_measure_a", "mag_measure_b", "mag_clear"};
    const char* labels[] = {"Read Lref", "Read bright spot", "Read dark spot", "Set Lref", "Adjust Lref", "Set Zone V time", "Adjust Zone V time", "Measure magnification A", "Measure magnification B", "Clear magnification compensation"};
    for (uint8_t i = 0; i < 10; ++i) {
        JsonObject item = commands.createNestedObject();
        item["id"] = ids[i]; item["label"] = labels[i];
    }
}

DEFINE_AND_REGISTER_ACTION_TYPE(meter_action_type,
    /* type_name   */ ACTION_TYPE_METER,
    /* parse       */ meter_parse,
    /* serialize   */ meter_serialize,
    /* dispatch    */ meter_action_dispatch,
    /* value_field */ meter_value_field,
    /* describe    */ meter_describe,
);

#endif // HAS_DISPLAY && IS_DARKROOM_TIMER
