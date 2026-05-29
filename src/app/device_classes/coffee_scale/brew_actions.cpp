// Coffee-brew ActionTypeDef — parse, serialize, resolve_bindings,
// has_binding, dispatch for the "brew" action type. Registered via
// REGISTER_ACTION_TYPE so action_dispatch.cpp / action_parse.cpp do not need
// to know about the brew arm at compile time.
//
// Phase 3 ships with stub brew engine functions. Phase 4 will replace the
// static-inline stubs at the top of this file with the real brew_manager.h
// include + linkage. The dispatcher logic itself does not change between
// phases.
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

#define TAG "BrewAction"

#if HAS_SCALE
// ---------------------------------------------------------------------------
// Phase 3 brew-engine stubs. Replaced in Phase 4 by an include of the real
// brew_manager.h. Kept static-inline here so they are local to this TU and
// drop cleanly when the real header arrives (no symbol-conflict risk).
// ---------------------------------------------------------------------------
static inline void brew_hint_template(const char* tpl) {
    LOGW(TAG, "brew_hint_template('%s') — Phase 4 brew engine not yet ported", tpl ? tpl : "");
}
static inline void brew_advance(void*) {
    LOGW(TAG, "brew_advance — Phase 4 brew engine not yet ported");
}
static inline void brew_start(void*) {
    LOGW(TAG, "brew_start — Phase 4 brew engine not yet ported");
}
static inline void brew_next() {
    LOGW(TAG, "brew_next — Phase 4 brew engine not yet ported");
}
static inline void brew_stop() {
    LOGW(TAG, "brew_stop — Phase 4 brew engine not yet ported");
}
static inline void brew_reset() {
    LOGW(TAG, "brew_reset — Phase 4 brew engine not yet ported");
}
#endif // HAS_SCALE

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

#if HAS_MQTT
// Only `value` is bindable (e.g. set_template using [list:brew_presets.selected]).
// `command` is structural and never bindable.
static void brew_resolve_bindings(ButtonAction& act) {
    char* field = brew_payload(act).value;
    if (field[0] && binding_template_has_bindings(field)) {
        char tmp[BINDING_TEMPLATE_MAX_LEN];
        binding_template_resolve(field, tmp, sizeof(tmp));
        strlcpy(field, tmp, sizeof(brew_payload(act).value));
    }
}

static bool brew_has_binding(const ButtonAction& act) {
    const char* f = brew_payload(act).value;
    return f[0] && memchr(f, '[', strlen(f)) != nullptr;
}
#endif

#if HAS_SCALE
static void brew_dispatch(const ButtonAction& act, const char* label) {
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
}
#else
static void brew_dispatch(const ButtonAction& /*act*/, const char* label) {
    LOGW(TAG, "%s brew: not compiled (HAS_SCALE=0)", label);
}
#endif

static const ActionTypeDef brew_action_type = {
    /* type_name        */ ACTION_TYPE_BREW,
    /* parse            */ brew_parse,
    /* serialize        */ brew_serialize,
#if HAS_MQTT
    /* resolve_bindings */ brew_resolve_bindings,
    /* has_binding      */ brew_has_binding,
#endif
    /* dispatch         */ brew_dispatch,
};

REGISTER_ACTION_TYPE(brew_action_type);

#endif // HAS_DISPLAY && IS_COFFEE_SCALE
