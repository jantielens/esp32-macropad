// Shutter Tester ActionTypeDef — parse, serialize, resolve_bindings,
// has_binding, dispatch for the "shutter" action type. Registered via
// REGISTER_ACTION_TYPE so action_dispatch.cpp / action_parse.cpp do not need
// to know about the shutter arm at compile time (beyond the union arm itself,
// which must live in pad_config.h because ActionPayload is a union).
//
// Aggregated into the build via device_classes/shutter_tester_device_class.cpp
// under #if IS_SHUTTER_TESTER.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_SHUTTER_TESTER

#include "../../log_manager.h"
#include "shutter_payload.h"
#include "shutter_capture.h"
#include "shutter_measure.h"
#include "shutter_session.h"

#if HAS_MQTT
#include "../../binding_template.h"
#endif

#include <string.h>

#define TAG "ShutterAction"

static void shutter_parse(const JsonObject& a, ButtonAction& act) {
    ShutterPayload& p = shutter_payload(act);
    strlcpy(p.command, a["shutter_command"] | "", sizeof(p.command));
    strlcpy(p.value,   a["shutter_value"]   | "", sizeof(p.value));
}

static void shutter_serialize(const ButtonAction& act, JsonObject obj) {
    const ShutterPayload& p = shutter_payload(act);
    if (p.command[0]) obj["shutter_command"] = p.command;
    if (p.value[0])   obj["shutter_value"]   = p.value;
}

#if HAS_MQTT
// The `value` field is user-templated for commands like guide_start
// ([list:shutter_tests.selected]) and sess_start (camera id from a list).
// `command` is structural (parsed by the dispatcher's strcmp ladder) and is
// never bindable.
static void shutter_resolve_bindings(ButtonAction& act) {
    char* field = shutter_payload(act).value;
    if (field[0] && binding_template_has_bindings(field)) {
        char tmp[BINDING_TEMPLATE_MAX_LEN];
        binding_template_resolve(field, tmp, sizeof(tmp));
        strlcpy(field, tmp, sizeof(shutter_payload(act).value));
    }
}

static bool shutter_has_binding(const ButtonAction& act) {
    const char* f = shutter_payload(act).value;
    return f[0] && memchr(f, '[', strlen(f)) != nullptr;
}
#endif

static void shutter_dispatch(const ButtonAction& act, const char* label) {
    const ShutterPayload& sh = shutter_payload(act);
    const char* cmd = sh.command;
    if (strcmp(cmd, "set") == 0) {
        if (!shutter_measure_set_target(sh.value)) {
            LOGW(TAG, "%s shutter set: unknown speed '%s'", label, sh.value);
        } else {
            LOGI(TAG, "%s shutter set: %s", label, sh.value);
        }
    } else if (strcmp(cmd, "adjust") == 0) {
        bool faster = strcmp(sh.value, "faster") == 0;
        shutter_measure_adjust_target(faster);
        LOGI(TAG, "%s shutter adjust: %s", label, sh.value);
    } else if (strcmp(cmd, "toggle_lock") == 0) {
        if (!shutter_measure_toggle_lock()) {
            LOGW(TAG, "%s shutter toggle_lock: no target set", label);
        } else {
            LOGI(TAG, "%s shutter toggle_lock", label);
        }
    } else if (strcmp(cmd, "sess_start") == 0) {
        shutter_session_start(sh.value);
        LOGI(TAG, "%s shutter sess_start: camera='%s'", label, sh.value);
    } else if (strcmp(cmd, "sess_stop") == 0) {
        shutter_session_stop();
        LOGI(TAG, "%s shutter sess_stop", label);
    } else if (strcmp(cmd, "sess_toggle") == 0) {
        shutter_session_toggle(sh.value);
        LOGI(TAG, "%s shutter sess_toggle: camera='%s'", label, sh.value);
    } else if (strcmp(cmd, "sess_discard") == 0) {
        shutter_session_discard_last();
        LOGI(TAG, "%s shutter sess_discard", label);
    } else if (strcmp(cmd, "guide_start") == 0) {
        shutter_session_guide_start(sh.value);
        LOGI(TAG, "%s shutter guide_start: test='%s'", label, sh.value);
    } else if (strcmp(cmd, "guide_stop") == 0) {
        shutter_session_guide_stop();
        LOGI(TAG, "%s shutter guide_stop", label);
    } else if (strcmp(cmd, "guide_skip") == 0) {
        shutter_session_guide_skip();
        LOGI(TAG, "%s shutter guide_skip", label);
    } else if (strcmp(cmd, "guide_redo") == 0) {
        shutter_session_guide_redo();
        LOGI(TAG, "%s shutter guide_redo", label);
    } else if (strcmp(cmd, "align_start") == 0) {
        shutter_capture_start_alignment();
        LOGI(TAG, "%s shutter align_start", label);
    } else if (strcmp(cmd, "align_stop") == 0) {
        shutter_capture_stop_alignment();
        LOGI(TAG, "%s shutter align_stop", label);
    } else if (strcmp(cmd, "recalibrate") == 0) {
        shutter_capture_recalibrate();
        LOGI(TAG, "%s shutter recalibrate", label);
    } else {
        LOGW(TAG, "%s shutter: unknown cmd '%s'", label, cmd);
    }
}

static const ActionTypeDef shutter_action_type = {
    /* type_name        */ ACTION_TYPE_SHUTTER,
    /* parse            */ shutter_parse,
    /* serialize        */ shutter_serialize,
#if HAS_MQTT
    /* resolve_bindings */ shutter_resolve_bindings,
    /* has_binding      */ shutter_has_binding,
#endif
    /* dispatch         */ shutter_dispatch,
};

REGISTER_ACTION_TYPE(shutter_action_type);

#endif // HAS_DISPLAY && IS_SHUTTER_TESTER
