// Shutter Tester ActionTypeDef — parse, serialize, dispatch and a value_field
// accessor for the "shutter" action type. Registered via
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

// The `value` field is the single bindable/numeric field: user-templated for
// commands like guide_start ([list:shutter_tests.selected]) and sess_start
// (camera id from a list). Expose it so shared code drives binding resolution
// + {step} substitution. `command` is structural and never bindable.
static char* shutter_value_field(ButtonAction& act, size_t* out_size) {
    ShutterPayload& p = shutter_payload(act);
    *out_size = sizeof(p.value);
    return p.value;
}

static void shutter_describe(JsonObject& out) {
    out["group"] = "Shutter Tester";
    out["label"] = "Shutter tester";
    JsonArray fields = out.createNestedArray("fields");
    JsonObject command_field = fields.createNestedObject();
    command_field["name"] = "shutter_command";
    command_field["description"] = "required command identifier";
    JsonObject value_field = fields.createNestedObject();
    value_field["name"] = "shutter_value";
    value_field["description"] = "optional value; target speed, adjustment direction, camera id, or guided-test id depending on the command; binding templates are supported";
    struct Command { const char* id; const char* label; };
    struct Family { const char* id; const char* label; const Command* commands; uint8_t count; };
    static const Command target_speed_cmds[] = {
        {"toggle_lock", "Toggle lock"}, {"set", "Set target speed"}, {"adjust", "Adjust target speed"}
    };
    static const Command session_cmds[] = {
        {"sess_toggle", "Toggle start/stop"}, {"sess_start", "Start"},
        {"sess_stop", "Stop"}, {"sess_discard", "Discard last shot"}
    };
    static const Command guided_test_cmds[] = {
        {"guide_start", "Start test"}, {"guide_stop", "Stop"},
        {"guide_skip", "Skip step"}, {"guide_redo", "Redo step"}
    };
    static const Command alignment_cmds[] = {
        {"align_start", "Start"}, {"align_stop", "Stop"}, {"recalibrate", "Recalibrate baseline"}
    };
    static const Family families[] = {
        {"target_speed", "Target speed", target_speed_cmds, 3},
        {"session", "Session", session_cmds, 4},
        {"guided_test", "Guided test", guided_test_cmds, 4},
        {"alignment", "Alignment", alignment_cmds, 3},
    };
    JsonArray out_families = out.createNestedArray("command_families");
    for (const Family& family : families) {
        JsonObject out_family = out_families.createNestedObject();
        out_family["id"] = family.id;
        out_family["label"] = family.label;
        JsonArray out_commands = out_family.createNestedArray("commands");
        for (uint8_t i = 0; i < family.count; ++i) {
            JsonObject item = out_commands.createNestedObject();
            item["id"] = family.commands[i].id;
            item["label"] = family.commands[i].label;
        }
    }
}

static ActionResult shutter_dispatch(const ButtonAction& act, const char* label,
                                     uint32_t /*continuation_token*/) {
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
    return ACTION_COMPLETE;
}

static const ActionTypeDef shutter_action_type = {
    /* type_name   */ ACTION_TYPE_SHUTTER,
    /* parse       */ shutter_parse,
    /* serialize   */ shutter_serialize,
    /* dispatch    */ shutter_dispatch,
    /* value_field */ shutter_value_field,
    /* describe    */ shutter_describe,
};

REGISTER_ACTION_TYPE(shutter_action_type);

#endif // HAS_DISPLAY && IS_SHUTTER_TESTER
