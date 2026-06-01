// Darkroom-timer ActionTypeDef — parse, serialize, dispatch for the "strip"
// (f-stop test strip) action type. Registered via REGISTER_ACTION_TYPE so
// action_dispatch.cpp / action_parse.cpp do not need to know about the strip
// arm at compile time.
//
// Aggregated into the build via
// device_classes/darkroom_timer/darkroom_timer_device_class.cpp
// under #if IS_DARKROOM_TIMER.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_DARKROOM_TIMER

#include "../../log_manager.h"
#include "darkroom_timer_payload.h"
#include "test_strip.h"

#include <string.h>

#define TAG "StripAction"

// Wire format: flat JSON {strip_command, strip_value} (legacy field names
// preserved for field-deployed pad-config backward compatibility).
static void strip_parse(const JsonObject& a, ButtonAction& act) {
    StripPayload& p = strip_payload(act);
    strlcpy(p.command, a["strip_command"] | "", sizeof(p.command));
    strlcpy(p.value,   a["strip_value"]   | "", sizeof(p.value));
}

static void strip_serialize(const ButtonAction& act, JsonObject obj) {
    const StripPayload& p = strip_payload(act);
    if (p.command[0]) obj["strip_command"] = p.command;
    if (p.value[0])   obj["strip_value"]   = p.value;
}

static void strip_dispatch(const ButtonAction& act, const char* label) {
    const StripPayload& p = strip_payload(act);
    if (!p.command[0]) {
        LOGW(TAG, "%s strip: empty command", label);
        return;
    }
    LOGI(TAG, "%s strip: %s %s", label, p.command, p.value);
    test_strip_dispatch(p.command, p.value);
}

// Numeric rocker drives adjust_* commands; substitute {step} into the value.
static void strip_substitute_step(ButtonAction& act, float step) {
    StripPayload& p = strip_payload(act);
    action_substitute_step_field(p.value, sizeof(p.value), step);
}

static const ActionTypeDef strip_action_type = {
    /* type_name        */ ACTION_TYPE_STRIP,
    /* parse            */ strip_parse,
    /* serialize        */ strip_serialize,
#if HAS_MQTT
    /* resolve_bindings */ nullptr,
    /* has_binding      */ nullptr,
#endif
    /* dispatch         */ strip_dispatch,
    /* substitute_step  */ strip_substitute_step,
};

REGISTER_ACTION_TYPE(strip_action_type);

#endif // HAS_DISPLAY && IS_DARKROOM_TIMER
