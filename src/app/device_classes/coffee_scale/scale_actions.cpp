// Coffee-scale ActionTypeDef — parse, serialize, dispatch and a value_field
// accessor for the "scale" action type. Registered via
// REGISTER_ACTION_TYPE so action_dispatch.cpp / action_parse.cpp do not need
// to know about the scale arm at compile time.
//
// Aggregated into the build via device_classes/coffee_scale/coffee_scale_device_class.cpp
// under #if IS_COFFEE_SCALE.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_COFFEE_SCALE

#include "../../log_manager.h"
#include "coffee_scale_payload.h"
#include "scale_hal.h"

#include <string.h>
#include <stdlib.h>

#define TAG "ScaleAction"

// Wire format: typed two-field JSON {scale_command, scale_value} mapping
// directly to ScalePayload — no string slicing, no mqtt_payload reuse.
static void scale_parse(const JsonObject& a, ButtonAction& act) {
    ScalePayload& p = scale_payload(act);
    strlcpy(p.command, a["scale_command"] | "", sizeof(p.command));
    strlcpy(p.value,   a["scale_value"]   | "", sizeof(p.value));
}

static void scale_serialize(const ButtonAction& act, JsonObject obj) {
    const ScalePayload& p = scale_payload(act);
    if (p.command[0]) obj["scale_command"] = p.command;
    if (p.value[0])   obj["scale_value"]   = p.value;
}

// `value` is the single bindable/numeric field: bindable (e.g. a [list:...]
// preset) and the numeric rocker {step} target for cal_weight. Expose it so
// shared code drives binding resolution + {step} substitution. `command` is
// structural and never bindable.
static char* scale_value_field(ButtonAction& act, size_t* out_size) {
    ScalePayload& p = scale_payload(act);
    *out_size = sizeof(p.value);
    return p.value;
}

static void scale_describe(JsonObject& out) {
    out["group"] = "Coffee Scale";
    out["label"] = "Scale";
    JsonArray fields = out.createNestedArray("fields");
    JsonObject command_field = fields.createNestedObject();
    command_field["name"] = "scale_command";
    command_field["description"] = "required command identifier";
    JsonObject value_field = fields.createNestedObject();
    value_field["name"] = "scale_value";
    value_field["description"] = "grams for cal_weight (signed adjustment) or cal_weight_set (absolute value); binding templates are supported";
    JsonArray commands = out.createNestedArray("commands");
    const char* ids[] = {"tare", "calibrate", "cal_weight", "cal_weight_set"};
    const char* labels[] = {"Tare", "Calibrate", "Adjust calibration weight", "Set calibration weight"};
    for (uint8_t i = 0; i < 4; ++i) {
        JsonObject item = commands.createNestedObject();
        item["id"] = ids[i]; item["label"] = labels[i];
    }
}

#if HAS_SCALE
static ActionResult scale_dispatch(const ButtonAction& act, const char* label,
                                   uint32_t /*continuation_token*/) {
    const ScalePayload& sp = scale_payload(act);
    const char* cmd = sp.command;
    if (!cmd[0] || strcmp(cmd, "tare") == 0) {
        LOGI(TAG, "%s scale: tare (deferred)", label);
        scale_request_tare();
    } else if (strcmp(cmd, "calibrate") == 0) {
        LOGI(TAG, "%s scale: calibrate (deferred)", label);
        scale_request_calibrate();
    } else if (strcmp(cmd, "cal_weight") == 0) {
        float delta = strtof(sp.value, nullptr);
        if (delta != 0.0f) {
            scale_adjust_cal_weight(delta);
            LOGI(TAG, "%s scale: cal_weight delta=%.1f -> %.1f g", label, delta, scale_get_cal_weight());
        } else {
            LOGW(TAG, "%s scale: cal_weight invalid delta '%s'", label, sp.value);
        }
    } else if (strcmp(cmd, "cal_weight_set") == 0) {
        float val = strtof(sp.value, nullptr);
        if (val >= 1.0f) {
            scale_set_cal_weight(val);
            LOGI(TAG, "%s scale: cal_weight_set %.1f g", label, scale_get_cal_weight());
        } else {
            LOGW(TAG, "%s scale: cal_weight_set invalid '%s'", label, sp.value);
        }
    } else {
        LOGW(TAG, "%s scale: unknown cmd '%s'", label, cmd);
    }
    return ACTION_COMPLETE;
}
#else
static ActionResult scale_dispatch(const ButtonAction& /*act*/, const char* label,
                                   uint32_t /*continuation_token*/) {
    LOGW(TAG, "%s scale: not compiled (HAS_SCALE=0)", label);
    return ACTION_COMPLETE;
}
#endif

DEFINE_AND_REGISTER_ACTION_TYPE(scale_action_type,
    /* type_name   */ ACTION_TYPE_SCALE,
    /* parse       */ scale_parse,
    /* serialize   */ scale_serialize,
    /* dispatch    */ scale_dispatch,
    /* value_field */ scale_value_field,
    /* describe    */ scale_describe,
);

#endif // HAS_DISPLAY && IS_COFFEE_SCALE
