#include "action_registry.h"
#if defined(ARDUINO) && HAS_DISPLAY
#include "display_manager.h"
#include "device_dialog.h"
#endif

#if HAS_DISPLAY
namespace {
const char* const buttonNavigationCommands[] = {"previous", "next", "tap", "tap_hold"};
const char* const buttonNavigationLabels[] = {"Previous", "Next", "Tap", "Tap & Hold"};

bool valid_button_navigation_command(const char* command) {
    if (!command) return false;
    for (const char* candidate : buttonNavigationCommands) {
        if (strcmp(candidate, command) == 0) return true;
    }
    return false;
}

void parse_button_navigation(const JsonObject& action, ButtonAction& act) {
    const char* command = action["command"] | "";
    if (!valid_button_navigation_command(command)) { memset(&act, 0, sizeof(act)); return; }
    strlcpy(act.payload.button_navigation.command, command, sizeof(act.payload.button_navigation.command));
}

void serialize_button_navigation(const ButtonAction& act, JsonObject action) {
    action["command"] = act.payload.button_navigation.command;
}

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

const char* validate_button_navigation(const JsonObjectConst action) {
    return action["command"].is<const char*>() && valid_button_navigation_command(action["command"].as<const char*>())
        ? nullptr : "button_nav command must be previous, next, tap, or tap_hold";
}

void describe_button_navigation(JsonObject& action) {
    action["group"] = "Navigation";
    action["label"] = "Navigate buttons";
    JsonArray commands = action.createNestedArray("commands");
    for (size_t index = 0; index < sizeof(buttonNavigationCommands) / sizeof(buttonNavigationCommands[0]); ++index) {
        JsonObject entry = commands.createNestedObject();
        entry["id"] = buttonNavigationCommands[index];
        entry["label"] = buttonNavigationLabels[index];
    }
    JsonArray fields = action.createNestedArray("fields");
    JsonObject field = fields.createNestedObject();
    field["name"] = "command";
    field["description"] = "previous, next, tap, or tap_hold";
    JsonArray editor = action.createNestedArray("editor_fields");
    JsonObject command = editor.createNestedObject();
    command["name"] = "command";
    command["label"] = "Command";
    command["type"] = "select";
    command["command_options"] = true;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kButtonNavigationActionType, ACTION_TYPE_BUTTON_NAVIGATION,
    parse_button_navigation, serialize_button_navigation, dispatch_button_navigation, nullptr,
    describe_button_navigation, nullptr, validate_button_navigation, nullptr,
    ACTION_EXECUTION_SYNC, false);
}
#endif