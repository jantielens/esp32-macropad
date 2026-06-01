// Darkroom-timer ActionTypeDef — parse, serialize, dispatch for the "print"
// (print session log) action type. Registered via REGISTER_ACTION_TYPE so
// action_dispatch.cpp / action_parse.cpp do not need to know about the print
// arm at compile time.
//
// Aggregated into the build via
// device_classes/darkroom_timer/darkroom_timer_device_class.cpp
// under #if IS_DARKROOM_TIMER.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_DARKROOM_TIMER

#include "../../log_manager.h"
#include "darkroom_timer_payload.h"
#include "print_log.h"

#include <string.h>

#define TAG "PrintAction"

// Wire format: flat JSON {print_command, print_value} (legacy field names
// preserved for field-deployed pad-config backward compatibility).
static void print_parse(const JsonObject& a, ButtonAction& act) {
    PrintPayload& p = print_payload(act);
    strlcpy(p.command, a["print_command"] | "", sizeof(p.command));
    strlcpy(p.value,   a["print_value"]   | "", sizeof(p.value));
}

static void print_serialize(const ButtonAction& act, JsonObject obj) {
    const PrintPayload& p = print_payload(act);
    if (p.command[0]) obj["print_command"] = p.command;
    if (p.value[0])   obj["print_value"]   = p.value;
}

static void print_dispatch(const ButtonAction& act, const char* label) {
    const PrintPayload& p = print_payload(act);
    if (!p.command[0]) {
        LOGW(TAG, "%s print: empty command", label);
        return;
    }
    LOGI(TAG, "%s print: %s %s", label, p.command, p.value);
    print_log_dispatch(p.command, p.value);
}

// Numeric rocker drives adjust_* commands; substitute {step} into the value.
static void print_substitute_step(ButtonAction& act, float step) {
    PrintPayload& p = print_payload(act);
    action_substitute_step_field(p.value, sizeof(p.value), step);
}

static const ActionTypeDef print_action_type = {
    /* type_name        */ ACTION_TYPE_PRINT,
    /* parse            */ print_parse,
    /* serialize        */ print_serialize,
#if HAS_MQTT
    /* resolve_bindings */ nullptr,
    /* has_binding      */ nullptr,
#endif
    /* dispatch         */ print_dispatch,
    /* substitute_step  */ print_substitute_step,
};

REGISTER_ACTION_TYPE(print_action_type);

#endif // HAS_DISPLAY && IS_DARKROOM_TIMER
