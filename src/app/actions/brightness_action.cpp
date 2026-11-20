#include "action_registry.h"
#include "log_manager.h"
#include <limits.h>
#if defined(ARDUINO) && HAS_DISPLAY
#include "display_manager.h"
#include "screen_saver_manager.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON

namespace {

constexpr const char* kBrightnessActionTag = "Action";

const ActionArgumentDef kBrightnessSetArguments[] = {
    {"percent", "Brightness (%)", ACTION_ARGUMENT_INTEGER, 0, 100, 0, true,
     "[var:brightness]", "number"},
};
const ActionArgumentDef kBrightnessAdjustArguments[] = {
    {"percent", "Adjust (%)", ACTION_ARGUMENT_INTEGER, INT32_MIN, INT32_MAX, 0, true,
     "[event:step]", "number"},
};

ActionResult set_brightness(const ActionResolvedArgument* arguments, const char* label, bool adjust) {
#if defined(ARDUINO) && HAS_DISPLAY
    int64_t value = arguments[0].integer;
    if (adjust) value += screen_saver_manager_get_brightness();
    if (value < MIN_USER_BRIGHTNESS) value = MIN_USER_BRIGHTNESS;
    if (value > 100) value = 100;
    screen_saver_manager_set_brightness(static_cast<uint8_t>(value));
    LOGI(kBrightnessActionTag, "%s brightness -> %u%%", label, static_cast<unsigned>(value));
    return ACTION_COMPLETE;
#else
    (void)arguments;
    (void)adjust;
    LOGW(kBrightnessActionTag, "%s brightness: no display", label);
    return ACTION_FAILED;
#endif
}

ActionResult dispatch_brightness_set(const ActionResolvedArgument* arguments, const char* label, uint32_t) {
    return set_brightness(arguments, label, false);
}
ActionResult dispatch_brightness_adjust(const ActionResolvedArgument* arguments, const char* label, uint32_t) {
    return set_brightness(arguments, label, true);
}
const ActionOperationDef kBrightnessOperations[] = {
    {"display.brightness.set", "Set brightness", kBrightnessSetArguments, 1, dispatch_brightness_set},
    {"display.brightness.adjust", "Adjust brightness", kBrightnessAdjustArguments, 1, dispatch_brightness_adjust},
};

bool brightness_available() { return HAS_DISPLAY; }

void describe_brightness(JsonObject& action) {
    action["group"] = "Display"; action["label"] = "Brightness";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kBrightnessActionType, ACTION_TYPE_BRIGHTNESS, describe_brightness, brightness_available, ACTION_EXECUTION_SYNC, true, false,
    kBrightnessOperations, sizeof(kBrightnessOperations) / sizeof(kBrightnessOperations[0]));

} // namespace

#endif // HAS_DISPLAY || HAS_BUTTON