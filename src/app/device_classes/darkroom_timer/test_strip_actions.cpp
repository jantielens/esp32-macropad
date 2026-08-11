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

static ActionResult strip_dispatch(const ButtonAction& act, const char* label,
                                   uint32_t /*continuation_token*/) {
    const StripPayload& p = strip_payload(act);
    if (!p.command[0]) {
        LOGW(TAG, "%s strip: empty command", label);
        return ACTION_COMPLETE;
    }
    LOGI(TAG, "%s strip: %s %s", label, p.command, p.value);
    test_strip_dispatch(p.command, p.value);
    return ACTION_COMPLETE;
}

// `value` is the single bindable/numeric field (numeric rocker {step} target
// for adjust_* commands); expose it so shared code drives binding + {step}.
static char* strip_value_field(ButtonAction& act, size_t* out_size) {
    StripPayload& p = strip_payload(act);
    *out_size = sizeof(p.value);
    return p.value;
}

static void strip_describe(JsonObject& out) {
    out["group"] = "Darkroom Timer";
    out["label"] = "Test strip";
    JsonArray fields = out.createNestedArray("fields");
    JsonObject command_field = fields.createNestedObject();
    command_field["name"] = "strip_command";
    command_field["description"] = "required command identifier";
    JsonObject value_field = fields.createNestedObject();
    value_field["name"] = "strip_value";
    value_field["description"] = "command-specific time, segment, or tick value; binding templates are supported";
    JsonArray commands = out.createNestedArray("commands");
    const char* ids[] = {"start", "cancel", "set_base", "adjust_base", "step_up", "step_down", "adjust_segments", "set_segments", "set_countdown", "adjust_countdown", "set_pause", "adjust_pause", "set_tick"};
    const char* labels[] = {"Start sequence", "Cancel sequence", "Set base time", "Adjust base time", "Increase step interval", "Decrease step interval", "Adjust segments", "Set segments", "Set initial countdown", "Adjust initial countdown", "Set pause duration", "Adjust pause duration", "Set exposure tick"};
    for (uint8_t i = 0; i < 13; ++i) {
        JsonObject item = commands.createNestedObject();
        item["id"] = ids[i]; item["label"] = labels[i];
    }
}

DEFINE_AND_REGISTER_ACTION_TYPE(strip_action_type,
    /* type_name   */ ACTION_TYPE_STRIP,
    /* parse       */ strip_parse,
    /* serialize   */ strip_serialize,
    /* dispatch    */ strip_dispatch,
    /* value_field */ strip_value_field,
    /* describe    */ strip_describe,
);

#endif // HAS_DISPLAY && IS_DARKROOM_TIMER
