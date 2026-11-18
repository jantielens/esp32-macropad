#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_DISPLAY
#include "config_manager.h"
extern DeviceConfig device_config;
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
ActionResult save_brightness(const char* label) {
#if defined(ARDUINO) && HAS_DISPLAY
    if (!config_manager_save_current_brightness(&device_config)) {
        LOGW("Action", "%s: failed to save brightness", label);
        return ACTION_FAILED;
    }
#else
    LOGW("Action", "%s: brightness saving unavailable", label);
    return ACTION_FAILED;
#endif
    return ACTION_COMPLETE;
}
ActionResult dispatch_save_brightness(const ButtonAction&, const char* label, uint32_t) {
#if !defined(ARDUINO) || !HAS_DISPLAY
    (void)label;
    return ACTION_COMPLETE;
#else
    return save_brightness(label);
#endif
}
ActionResult dispatch_save_brightness_operation(const ActionResolvedArgument*, const char* label, uint32_t) {
    return save_brightness(label);
}
const ActionOperationDef kSaveBrightnessOperations[] = {
    {"display.brightness.save", "Save brightness", nullptr, 0, dispatch_save_brightness_operation},
};
bool save_brightness_available() { return HAS_DISPLAY; }
void describe_save_brightness(JsonObject& action) {
    action["group"] = "Display";
    action["label"] = "Save brightness";
}
DEFINE_AND_REGISTER_ACTION_TYPE(kSaveBrightnessActionType, ACTION_TYPE_SAVE_BRIGHTNESS, nullptr, nullptr, dispatch_save_brightness, nullptr, describe_save_brightness, save_brightness_available, nullptr, ACTION_EXECUTION_SYNC, false, true, false, nullptr, kSaveBrightnessOperations, 1);
}
#endif