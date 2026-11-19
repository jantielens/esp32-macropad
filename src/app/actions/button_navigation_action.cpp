#include "action_registry.h"
#if defined(ARDUINO) && HAS_DISPLAY && HAS_PSRAM
#include "item_binding.h"
#endif
#if defined(ARDUINO) && HAS_DISPLAY
#include "display_manager.h"
#include "device_dialog.h"
#endif

#if HAS_DISPLAY
namespace {
const char* const buttonNavigationCommands[] = {"previous", "next", "tap", "tap_hold"};
const char* const buttonNavigationLabels[] = {"Previous", "Next", "Tap", "Tap & Hold"};

ActionResult dispatch_button_navigation(const ButtonAction& act, const char*, uint32_t) {
#if defined(ARDUINO) && HAS_DISPLAY
    if (!displayManager) return ACTION_FAILED;
    struct NavigationRequest {
        ButtonNavigationPayload navigation;
        uint32_t generation;
    };
    const NavigationRequest request{act.payload.button_navigation, device_dialog_input_generation()};
    bool ok = false;
    const auto exec = [](const void* context, bool* success, char*, size_t) {
        const auto& request = *static_cast<const NavigationRequest*>(context);
        *success = request.generation == device_dialog_input_generation() &&
            displayManager->navigateButtons(request.navigation.command);
    };
    const auto result = display_manager_dispatch(exec, nullptr, &request,
        sizeof(request), 1000, &ok, nullptr, 0);
    return result == DISPLAY_TASK_DISPATCH_OK && ok ? ACTION_COMPLETE : ACTION_FAILED;
#else
    (void)act;
    return ACTION_FAILED;
#endif
}

ActionResult dispatch_button_navigation_operation(const ActionResolvedArgument* arguments, const char* label, uint32_t token) {
#if defined(ARDUINO) && HAS_PSRAM
    if (!strcmp(arguments[0].text, "tap") || !strcmp(arguments[0].text, "tap_hold"))
        ItemBindingScope::end_current();
#endif
    ButtonAction action = {};
    strlcpy(action.payload.button_navigation.command, arguments[0].text,
            sizeof(action.payload.button_navigation.command));
    return dispatch_button_navigation(action, label, token);
}

const ActionArgumentDef kButtonNavigationArguments[] = {
    {"command", "Command", ACTION_ARGUMENT_ENUM, 0, 0, sizeof(ButtonNavigationPayload::command) - 1,
     true, "[var:command]", "select", buttonNavigationCommands, 4, false, nullptr, buttonNavigationLabels},
};
const ActionOperationDef kButtonNavigationOperations[] = {
    {"button.navigate", "Navigate buttons", kButtonNavigationArguments, 1, dispatch_button_navigation_operation},
};

void describe_button_navigation(JsonObject& action) {
    action["group"] = "Navigation";
    action["label"] = "Navigate buttons";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kButtonNavigationActionType, ACTION_TYPE_BUTTON_NAVIGATION, describe_button_navigation, nullptr,
    ACTION_EXECUTION_SYNC, false, false, kButtonNavigationOperations, 1);
}
#endif