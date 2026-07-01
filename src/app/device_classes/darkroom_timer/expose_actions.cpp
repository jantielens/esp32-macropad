// Darkroom-timer ActionTypeDef — parse, serialize, dispatch for the "expose"
// action type. Registered via REGISTER_ACTION_TYPE so action_dispatch.cpp /
// action_parse.cpp do not need to know about the expose arm at compile time.
//
// Aggregated into the build via
// device_classes/darkroom_timer/darkroom_timer_device_class.cpp
// under #if IS_DARKROOM_TIMER.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_DARKROOM_TIMER

#include "../../log_manager.h"
#include "darkroom_timer_payload.h"
#include "expose_timer.h"

#include <string.h>

#define TAG "ExposeAction"

// Wire format: flat JSON {expose_command, expose_value} (legacy field names
// preserved for field-deployed pad-config backward compatibility).
static void expose_parse(const JsonObject& a, ButtonAction& act) {
    ExposePayload& p = expose_payload(act);
    strlcpy(p.command, a["expose_command"] | "", sizeof(p.command));
    strlcpy(p.value,   a["expose_value"]   | "", sizeof(p.value));
}

static void expose_serialize(const ButtonAction& act, JsonObject obj) {
    const ExposePayload& p = expose_payload(act);
    if (p.command[0]) obj["expose_command"] = p.command;
    if (p.value[0])   obj["expose_value"]   = p.value;
}

static void expose_dispatch(const ButtonAction& act, const char* label) {
    const ExposePayload& p = expose_payload(act);
    if (!p.command[0]) {
        LOGW(TAG, "%s expose: empty command", label);
        return;
    }
    LOGI(TAG, "%s expose: %s %s", label, p.command, p.value);
    expose_timer_dispatch(p.command, p.value);
}

// `value` is the single bindable/numeric field (numeric rocker {step} target
// for adjust_* commands); expose it so shared code drives binding + {step}.
static char* expose_value_field(ButtonAction& act, size_t* out_size) {
    ExposePayload& p = expose_payload(act);
    *out_size = sizeof(p.value);
    return p.value;
}

static const ActionTypeDef expose_action_type = {
    /* type_name   */ ACTION_TYPE_EXPOSE,
    /* parse       */ expose_parse,
    /* serialize   */ expose_serialize,
    /* dispatch    */ expose_dispatch,
    /* value_field */ expose_value_field,
};

REGISTER_ACTION_TYPE(expose_action_type);

#endif // HAS_DISPLAY && IS_DARKROOM_TIMER
