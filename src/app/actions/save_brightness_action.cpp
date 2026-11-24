#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_DISPLAY
#include "screen_saver_manager.h"
#include "save_level_action.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
ActionResult dispatch_save_brightness_operation(const ActionResolvedArgument*, const char* label, uint32_t token) {
#if defined(ARDUINO) && HAS_DISPLAY
    return queue_save_level(screen_saver_manager_get_brightness(), config_manager_save_brightness, "brightness", label, token);
#else
    (void)token;
    LOGW("Action", "%s: brightness saving unavailable", label);
    return ACTION_FAILED;
#endif
}
const ActionOperationDef kSaveBrightnessOperations[] = {
    {"display.brightness.save", "Save brightness", nullptr, 0, dispatch_save_brightness_operation, ACTION_EXECUTION_PAUSABLE},
};
bool save_brightness_available() { return HAS_DISPLAY; }
void describe_save_brightness(JsonObject& action) {
    action["group"] = "Display";
    action["label"] = "Save brightness";
}
DEFINE_AND_REGISTER_ACTION_TYPE(kSaveBrightnessActionType, ACTION_TYPE_SAVE_BRIGHTNESS, describe_save_brightness, save_brightness_available, ACTION_EXECUTION_PAUSABLE, false, false, kSaveBrightnessOperations, 1);
}
#endif