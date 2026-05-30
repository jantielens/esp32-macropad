// Coffee-scale ActionTypeDef — parse, serialize, resolve_bindings,
// has_binding, dispatch for the "scale" action type. Registered via
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

#if HAS_MQTT
#include "../../binding_template.h"
#endif

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

#if HAS_MQTT
// Only `value` is bindable (numeric, e.g. from a [list:...] preset).
// `command` is structural and never bindable.
static void scale_resolve_bindings(ButtonAction& act) {
    char* field = scale_payload(act).value;
    if (field[0] && binding_template_has_bindings(field)) {
        char tmp[BINDING_TEMPLATE_MAX_LEN];
        binding_template_resolve(field, tmp, sizeof(tmp));
        strlcpy(field, tmp, sizeof(scale_payload(act).value));
    }
}

static bool scale_has_binding(const ButtonAction& act) {
    const char* f = scale_payload(act).value;
    return f[0] && memchr(f, '[', strlen(f)) != nullptr;
}
#endif

#if HAS_SCALE
static void scale_dispatch(const ButtonAction& act, const char* label) {
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
}
#else
static void scale_dispatch(const ButtonAction& /*act*/, const char* label) {
    LOGW(TAG, "%s scale: not compiled (HAS_SCALE=0)", label);
}
#endif

static const ActionTypeDef scale_action_type = {
    /* type_name        */ ACTION_TYPE_SCALE,
    /* parse            */ scale_parse,
    /* serialize        */ scale_serialize,
#if HAS_MQTT
    /* resolve_bindings */ scale_resolve_bindings,
    /* has_binding      */ scale_has_binding,
#endif
    /* dispatch         */ scale_dispatch,
};

REGISTER_ACTION_TYPE(scale_action_type);

#endif // HAS_DISPLAY && IS_COFFEE_SCALE
