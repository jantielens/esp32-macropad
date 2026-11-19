// Shutter Tester operations, aggregated by shutter_tester_device_class.cpp.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_SHUTTER_TESTER

#include "../../log_manager.h"
#include "shutter_payload.h"
#include "shutter_capture.h"
#include "shutter_measure.h"
#include "shutter_session.h"

#include <string.h>

#define TAG "ShutterAction"

static void shutter_describe(JsonObject& out) {
    out["group"] = "Shutter Tester";
    out["label"] = "Shutter tester";
}

static const char* const shutter_speed_choices[] = {
    "4s", "2s", "1s", "1/2s", "1/4s", "1/5s", "1/8s", "1/10s", "1/15s",
    "1/25s", "1/30s", "1/50s", "1/60s", "1/100s", "1/125s", "1/200s",
    "1/250s", "1/500s", "1/1000s", "1/2000s",
    "4", "2", "1", "1/2", "1/4", "1/5", "1/8", "1/10", "1/15",
    "1/25", "1/30", "1/50", "1/60", "1/100", "1/125", "1/200",
    "1/250", "1/500", "1/1000", "1/2000"
};
static const char* const shutter_direction_choices[] = {"faster", "slower"};
static const char* const shutter_direction_labels[] = {"Faster (shorter)", "Slower (longer)"};
static const ActionArgumentDef shutter_speed_arguments[] = {
    {"speed", "Target speed", ACTION_ARGUMENT_ENUM, 0, 0, 8, true,
     "[item:speed]", "select", shutter_speed_choices,
     sizeof(shutter_speed_choices) / sizeof(shutter_speed_choices[0])},
};
static const ActionArgumentDef shutter_direction_arguments[] = {
    {"direction", "Direction", ACTION_ARGUMENT_ENUM, 0, 0, 6, true,
     "[var:direction]", "select", shutter_direction_choices, 2, false, nullptr, shutter_direction_labels},
};
static const ActionArgumentDef shutter_camera_arguments[] = {
    {"camera", "Camera (optional)", ACTION_ARGUMENT_STRING, 0, 0, CONFIG_BINDABLE_SHORT_LEN - 1,
     true, "[item:id]", "text", nullptr, 0, true},
};
static const ActionArgumentDef shutter_test_arguments[] = {
    {"test", "Test ID", ACTION_ARGUMENT_STRING, 0, 0, CONFIG_BINDABLE_SHORT_LEN - 1,
     true, "[item:id]", "text"},
};
static ActionResult shutter_set_op(const ActionResolvedArgument* args, const char* label, uint32_t) {
    if (!shutter_measure_set_target(args[0].text)) {
        LOGW(TAG, "%s shutter set: unknown speed '%s'", label, args[0].text);
        return ACTION_FAILED;
    }
    return ACTION_COMPLETE;
}
static ActionResult shutter_adjust_op(const ActionResolvedArgument* args, const char*, uint32_t) {
    shutter_measure_adjust_target(!strcmp(args[0].text, "faster"));
    return ACTION_COMPLETE;
}
static ActionResult shutter_lock_op(const ActionResolvedArgument*, const char* label, uint32_t) {
    if (!shutter_measure_toggle_lock()) {
        LOGW(TAG, "%s shutter toggle_lock: no target set", label);
        return ACTION_FAILED;
    }
    return ACTION_COMPLETE;
}
static ActionResult shutter_session_start_op(const ActionResolvedArgument* args, const char*, uint32_t) {
    return shutter_session_start(args[0].text) ? ACTION_COMPLETE : ACTION_FAILED;
}
static ActionResult shutter_session_toggle_op(const ActionResolvedArgument* args, const char*, uint32_t) {
    return shutter_session_toggle(args[0].text) ? ACTION_COMPLETE : ACTION_FAILED;
}
static ActionResult shutter_guide_start_op(const ActionResolvedArgument* args, const char*, uint32_t) {
    return shutter_session_guide_start(args[0].text) ? ACTION_COMPLETE : ACTION_FAILED;
}
#define SHUTTER_SIMPLE(fn, call) \
    static ActionResult fn(const ActionResolvedArgument*, const char*, uint32_t) { \
        call(); return ACTION_COMPLETE; \
    }
SHUTTER_SIMPLE(shutter_session_stop_op, shutter_session_stop)
SHUTTER_SIMPLE(shutter_session_discard_op, shutter_session_discard_last)
SHUTTER_SIMPLE(shutter_guide_stop_op, shutter_session_guide_stop)
SHUTTER_SIMPLE(shutter_guide_skip_op, shutter_session_guide_skip)
SHUTTER_SIMPLE(shutter_guide_redo_op, shutter_session_guide_redo)
SHUTTER_SIMPLE(shutter_align_start_op, shutter_capture_start_alignment)
SHUTTER_SIMPLE(shutter_align_stop_op, shutter_capture_stop_alignment)
SHUTTER_SIMPLE(shutter_recalibrate_op, shutter_capture_recalibrate)
#undef SHUTTER_SIMPLE
static const ActionOperationDef shutter_operations[] = {
    {"shutter.target.set", "Set target speed", shutter_speed_arguments, 1, shutter_set_op},
    {"shutter.target.adjust", "Adjust target speed", shutter_direction_arguments, 1, shutter_adjust_op},
    {"shutter.target.toggle_lock", "Toggle target lock", nullptr, 0, shutter_lock_op},
    {"shutter.session.start", "Start shutter session", shutter_camera_arguments, 1, shutter_session_start_op},
    {"shutter.session.toggle", "Toggle shutter session", shutter_camera_arguments, 1, shutter_session_toggle_op},
    {"shutter.session.stop", "Stop shutter session", nullptr, 0, shutter_session_stop_op},
    {"shutter.session.discard", "Discard last shot", nullptr, 0, shutter_session_discard_op},
    {"shutter.guide.start", "Start guided test", shutter_test_arguments, 1, shutter_guide_start_op},
    {"shutter.guide.stop", "Stop guided test", nullptr, 0, shutter_guide_stop_op},
    {"shutter.guide.skip", "Skip guided step", nullptr, 0, shutter_guide_skip_op},
    {"shutter.guide.redo", "Redo guided step", nullptr, 0, shutter_guide_redo_op},
    {"shutter.alignment.start", "Start alignment", nullptr, 0, shutter_align_start_op},
    {"shutter.alignment.stop", "Stop alignment", nullptr, 0, shutter_align_stop_op},
    {"shutter.recalibrate", "Recalibrate baseline", nullptr, 0, shutter_recalibrate_op},
};

DEFINE_AND_REGISTER_ACTION_TYPE(shutter_action_type, ACTION_TYPE_SHUTTER, shutter_describe, nullptr, ACTION_EXECUTION_SYNC,
    true, false, shutter_operations, sizeof(shutter_operations) / sizeof(shutter_operations[0]));

#endif // HAS_DISPLAY && IS_SHUTTER_TESTER
