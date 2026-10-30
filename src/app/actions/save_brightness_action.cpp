#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_DISPLAY
#include "config_manager.h"
extern DeviceConfig device_config;
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
ActionResult dispatch_save_brightness(const ButtonAction&, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_DISPLAY
    if (!config_manager_save_current_brightness(&device_config)) {
        LOGW("Action", "%s: failed to save brightness", label);
        return ACTION_FAILED;
    }
#else
    (void)label;
#endif
    return ACTION_COMPLETE;
}
bool save_brightness_available() { return HAS_DISPLAY; }
void describe_save_brightness(JsonObject& action) {
    action["group"] = "Display";
    action["label"] = "Save brightness";
}
DEFINE_AND_REGISTER_ACTION_TYPE(kSaveBrightnessActionType, ACTION_TYPE_SAVE_BRIGHTNESS, nullptr, nullptr, dispatch_save_brightness, nullptr, describe_save_brightness, save_brightness_available, nullptr, ACTION_EXECUTION_SYNC, false);
}
#endif