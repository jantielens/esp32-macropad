// Coffee-scale operations, aggregated by coffee_scale_device_class.cpp.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_COFFEE_SCALE

#include "../../log_manager.h"
#include "coffee_scale_payload.h"
#include "scale_hal.h"

#include <limits.h>

#define TAG "ScaleAction"

static void scale_describe(JsonObject& out) {
    out["group"] = "Coffee Scale";
    out["label"] = "Scale";
}

#if HAS_SCALE
static const ActionArgumentDef scale_adjust_arguments[] = {
    {"grams", "Weight change (g)", ACTION_ARGUMENT_NUMBER, INT32_MIN, INT32_MAX, 0,
     true, "[event:step]", "number"},
};
static const ActionArgumentDef scale_set_arguments[] = {
    {"grams", "Calibration weight (g)", ACTION_ARGUMENT_NUMBER, 1, INT32_MAX, 0,
     true, "[var:calibration_weight]", "number"},
};
static ActionResult scale_tare_operation(const ActionResolvedArgument*, const char*, uint32_t) {
    scale_request_tare();
    return ACTION_COMPLETE;
}
static ActionResult scale_calibrate_operation(const ActionResolvedArgument*, const char*, uint32_t) {
    scale_request_calibrate();
    return ACTION_COMPLETE;
}
static ActionResult scale_adjust_operation(const ActionResolvedArgument* args, const char* label, uint32_t) {
    if (args[0].number == 0) {
        LOGW(TAG, "%s scale: zero calibration adjustment", label);
        return ACTION_FAILED;
    }
    scale_adjust_cal_weight(args[0].number);
    return ACTION_COMPLETE;
}
static ActionResult scale_set_operation(const ActionResolvedArgument* args, const char*, uint32_t) {
    scale_set_cal_weight(args[0].number);
    return ACTION_COMPLETE;
}
static const ActionOperationDef scale_operations[] = {
    {"scale.tare", "Tare scale", nullptr, 0, scale_tare_operation},
    {"scale.calibrate", "Calibrate scale", nullptr, 0, scale_calibrate_operation},
    {"scale.calibration.adjust_weight", "Adjust calibration weight", scale_adjust_arguments, 1, scale_adjust_operation},
    {"scale.calibration.set_weight", "Set calibration weight", scale_set_arguments, 1, scale_set_operation},
};

#endif

#if HAS_SCALE
DEFINE_AND_REGISTER_ACTION_TYPE(scale_action_type, ACTION_TYPE_SCALE, scale_describe, nullptr, ACTION_EXECUTION_SYNC,
    true, false, scale_operations, sizeof(scale_operations) / sizeof(scale_operations[0]));
#else
DEFINE_AND_REGISTER_ACTION_TYPE(scale_action_type, ACTION_TYPE_SCALE, scale_describe, nullptr, ACTION_EXECUTION_SYNC);
#endif

#endif // HAS_DISPLAY && IS_COFFEE_SCALE
