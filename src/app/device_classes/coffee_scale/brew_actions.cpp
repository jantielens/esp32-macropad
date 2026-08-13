// Coffee-brew ActionTypeDef — parse, serialize, dispatch and a value_field
// accessor for the "brew" action type. Registered via
// REGISTER_ACTION_TYPE so action_dispatch.cpp / action_parse.cpp do not need
// to know about the brew arm at compile time.
//
// The dispatcher resolves a brew command (start / next / stop / reset / tare /
// advance / set_template) and drives the brew engine state machine in
// brew_manager.
//
// Aggregated into the build via device_classes/coffee_scale/coffee_scale_device_class.cpp
// under #if IS_COFFEE_SCALE.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_COFFEE_SCALE

#include "../../log_manager.h"
#include "coffee_scale_payload.h"
#include "scale_hal.h"
#include "brew/brew_manager.h"

#include <string.h>

#define TAG "BrewAction"

// Wire format: typed two-field JSON {brew_command, brew_value} mapping
// directly to BrewPayload — no string slicing, no mqtt_payload reuse.
static void brew_parse(const JsonObject& a, ButtonAction& act) {
    BrewPayload& p = brew_payload(act);
    strlcpy(p.command, a["brew_command"] | "", sizeof(p.command));
    strlcpy(p.value,   a["brew_value"]   | "", sizeof(p.value));
}

static void brew_serialize(const ButtonAction& act, JsonObject obj) {
    const BrewPayload& p = brew_payload(act);
    if (p.command[0]) obj["brew_command"] = p.command;
    if (p.value[0])   obj["brew_value"]   = p.value;
}

// `value` is the single bindable field (e.g. set_template using
// [list:brew_presets.selected]). Expose it so shared code drives binding
// resolution + {step} substitution. `command` is structural, never bindable.
static char* brew_value_field(ButtonAction& act, size_t* out_size) {
    BrewPayload& p = brew_payload(act);
    *out_size = sizeof(p.value);
    return p.value;
}

static void brew_describe(JsonObject& out) {
    out["group"] = "Coffee Scale";
    out["label"] = "Brew";
    JsonArray fields = out.createNestedArray("fields");
    JsonObject command_field = fields.createNestedObject();
    command_field["name"] = "brew_command";
    command_field["description"] = "required command identifier";
    JsonObject value_field = fields.createNestedObject();
    value_field["name"] = "brew_value";
    value_field["description"] = "template name for set_template; binding templates are supported";
    JsonArray commands = out.createNestedArray("commands");
    const char* ids[] = {"advance", "start", "next", "stop", "reset", "tare", "set_template"};
    const char* labels[] = {"Advance", "Start", "Next step", "Stop", "Reset", "Tare", "Set template"};
    for (uint8_t i = 0; i < 7; ++i) {
        JsonObject item = commands.createNestedObject();
        item["id"] = ids[i]; item["label"] = labels[i];
    }
}

#if HAS_SCALE
static ActionResult brew_dispatch(const ButtonAction& act, const char* label,
                                  uint32_t /*continuation_token*/) {
    const BrewPayload& bp = brew_payload(act);
    const char* cmd = bp.command;
    // Empty command defaults to "advance" — preserves legacy
    // feature/coffee-scale behaviour for buttons authored without an explicit
    // command. Safe to drop later if undesired.
    if (!cmd[0]) cmd = "advance";

    if (strcmp(cmd, "set_template") == 0) {
        LOGI(TAG, "%s brew: set_template='%s'", label, bp.value);
        brew_hint_template(bp.value);
    } else if (strcmp(cmd, "advance") == 0) {
        LOGI(TAG, "%s brew: advance", label);
        brew_advance(nullptr);
    } else if (strcmp(cmd, "start") == 0) {
        LOGI(TAG, "%s brew: start", label);
        brew_start(nullptr);
    } else if (strcmp(cmd, "next") == 0) {
        LOGI(TAG, "%s brew: next", label);
        brew_next();
    } else if (strcmp(cmd, "stop") == 0) {
        LOGI(TAG, "%s brew: stop", label);
        brew_stop();
    } else if (strcmp(cmd, "reset") == 0) {
        LOGI(TAG, "%s brew: reset", label);
        brew_reset();
    } else if (strcmp(cmd, "tare") == 0) {
        LOGI(TAG, "%s brew: tare", label);
        scale_request_tare_no_persist();
    } else {
        LOGW(TAG, "%s brew: unknown cmd '%s'", label, cmd);
    }
    return ACTION_COMPLETE;
}
#else
static ActionResult brew_dispatch(const ButtonAction& /*act*/, const char* label,
                                  uint32_t /*continuation_token*/) {
    LOGW(TAG, "%s brew: not compiled (HAS_SCALE=0)", label);
    return ACTION_COMPLETE;
}
#endif

DEFINE_AND_REGISTER_ACTION_TYPE(brew_action_type,
    /* type_name   */ ACTION_TYPE_BREW,
    /* parse       */ brew_parse,
    /* serialize   */ brew_serialize,
    /* dispatch    */ brew_dispatch,
    /* value_field */ brew_value_field,
    /* describe    */ brew_describe,
);

#endif // HAS_DISPLAY && IS_COFFEE_SCALE
