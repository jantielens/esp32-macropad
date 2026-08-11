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

static ActionResult expose_dispatch(const ButtonAction& act, const char* label,
                                    uint32_t /*continuation_token*/) {
    const ExposePayload& p = expose_payload(act);
    if (!p.command[0]) {
        LOGW(TAG, "%s expose: empty command", label);
        return ACTION_COMPLETE;
    }
    LOGI(TAG, "%s expose: %s %s", label, p.command, p.value);
    expose_timer_dispatch(p.command, p.value);
    return ACTION_COMPLETE;
}

// `value` is the single bindable/numeric field (numeric rocker {step} target
// for adjust_* commands); expose it so shared code drives binding + {step}.
static char* expose_value_field(ButtonAction& act, size_t* out_size) {
    ExposePayload& p = expose_payload(act);
    *out_size = sizeof(p.value);
    return p.value;
}

static void expose_describe(JsonObject& out) {
    out["group"] = "Darkroom Timer";
    out["label"] = "Exposure timer";
    JsonArray fields = out.createNestedArray("fields");
    JsonObject command_field = fields.createNestedObject();
    command_field["name"] = "expose_command";
    command_field["description"] = "required command identifier";
    JsonObject value_field = fields.createNestedObject();
    value_field["name"] = "expose_value";
    value_field["description"] = "seconds for set_time/adjust_seconds, f-stops for adjust_stops, or percentage points for dry-down commands; binding templates are supported";
    JsonArray commands = out.createNestedArray("commands");
    const char* ids[] = {"toggle", "start", "stop", "pause", "resume", "reset", "focus", "focus_off", "focus_toggle", "set_time", "adjust_seconds", "adjust_stops", "set_dry_down", "adjust_dry_down"};
    const char* labels[] = {"Toggle start/pause/resume", "Start", "Stop", "Pause", "Resume", "Reset", "Focus light on", "Focus light off", "Toggle focus light", "Set time", "Adjust seconds", "Adjust f-stops", "Set dry-down", "Adjust dry-down"};
    for (uint8_t i = 0; i < 14; ++i) {
        JsonObject item = commands.createNestedObject();
        item["id"] = ids[i]; item["label"] = labels[i];
    }
}

DEFINE_AND_REGISTER_ACTION_TYPE(expose_action_type,
    /* type_name   */ ACTION_TYPE_EXPOSE,
    /* parse       */ expose_parse,
    /* serialize   */ expose_serialize,
    /* dispatch    */ expose_dispatch,
    /* value_field */ expose_value_field,
    /* describe    */ expose_describe,
);

#endif // HAS_DISPLAY && IS_DARKROOM_TIMER
