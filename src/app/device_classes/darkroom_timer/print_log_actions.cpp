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

static ActionResult print_dispatch(const ButtonAction& act, const char* label,
                                   uint32_t /*continuation_token*/) {
    const PrintPayload& p = print_payload(act);
    if (!p.command[0]) {
        LOGW(TAG, "%s print: empty command", label);
        return ACTION_COMPLETE;
    }
    LOGI(TAG, "%s print: %s %s", label, p.command, p.value);
    print_log_dispatch(p.command, p.value);
    return ACTION_COMPLETE;
}

// `value` is the single bindable/numeric field (numeric rocker {step} target
// for adjust_* commands); expose it so shared code drives binding + {step}.
static char* print_value_field(ButtonAction& act, size_t* out_size) {
    PrintPayload& p = print_payload(act);
    *out_size = sizeof(p.value);
    return p.value;
}

static void print_describe(JsonObject& out) {
    out["group"] = "Darkroom Timer";
    out["label"] = "Print log";
    JsonArray fields = out.createNestedArray("fields");
    JsonObject command_field = fields.createNestedObject();
    command_field["name"] = "print_command";
    command_field["description"] = "required command identifier";
    JsonObject value_field = fields.createNestedObject();
    value_field["name"] = "print_value";
    value_field["description"] = "1 to star or 0 to unstar when print_command is set_star; binding templates are supported";
    JsonArray commands = out.createNestedArray("commands");
    const char* ids[] = {"toggle_star", "set_star"};
    const char* labels[] = {"Toggle star", "Set star"};
    for (uint8_t i = 0; i < 2; ++i) {
        JsonObject item = commands.createNestedObject();
        item["id"] = ids[i]; item["label"] = labels[i];
    }
}

DEFINE_AND_REGISTER_ACTION_TYPE(print_action_type,
    /* type_name   */ ACTION_TYPE_PRINT,
    /* parse       */ print_parse,
    /* serialize   */ print_serialize,
    /* dispatch    */ print_dispatch,
    /* value_field */ print_value_field,
    /* describe    */ print_describe,
);

#endif // HAS_DISPLAY && IS_DARKROOM_TIMER
