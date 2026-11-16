#include "action_registry.h"

#if HAS_DISPLAY && HAS_PSRAM

#include "../widgets/list_config.h"
#if defined(ARDUINO)
#include "../display_manager.h"
#include "../device_dialog.h"
#include "../log_manager.h"
#endif

namespace {
const char* const commands[] = {"previous_page", "next_page"};
const char* const labels[] = {"Previous page", "Next page"};

bool valid_command(const char* command) {
    if (!command) return false;
    for (const char* candidate : commands)
        if (!strcmp(candidate, command)) return true;
    return false;
}

const char* validate_list_navigation(JsonObjectConst action) {
    if (!action["target"].is<const char*>() ||
        !list_identifier_valid(action["target"].as<const char*>()))
        return "list_nav target must be a List ID";
    if (!action["command"].is<const char*>() ||
        !valid_command(action["command"].as<const char*>()))
        return "list_nav command must be previous_page or next_page";
    return nullptr;
}

void parse_list_navigation(const JsonObject& action, ButtonAction& result) {
    if (validate_list_navigation(action)) { memset(&result, 0, sizeof(result)); return; }
    strlcpy(result.payload.list_navigation.target, action["target"], sizeof(result.payload.list_navigation.target));
    strlcpy(result.payload.list_navigation.command, action["command"], sizeof(result.payload.list_navigation.command));
}

void serialize_list_navigation(const ButtonAction& action, JsonObject result) {
    result["target"] = action.payload.list_navigation.target;
    result["command"] = action.payload.list_navigation.command;
}

ActionResult dispatch_list_navigation(const ButtonAction& action, const char*, uint32_t) {
#if defined(ARDUINO)
    if (!displayManager) {
        LOGW("ListNav", "Display unavailable");
        return ACTION_FAILED;
    }
    struct Request {
        ListNavigationPayload navigation;
        uint32_t screen_generation;
        uint32_t input_generation;
        uint32_t pad_generation;
    };
    Request request{action.payload.list_navigation, 0, device_dialog_input_generation(),
                    pad_config_get_generation()};
    if (!displayManager->captureListNavigation(&request.screen_generation)) {
        LOGW("ListNav", "No active pad available for List navigation");
        return ACTION_FAILED;
    }
    bool ok = false;
    const auto execute = [](const void* context, bool* success, char*, size_t) {
        const auto& request = *static_cast<const Request*>(context);
        *success = request.input_generation == device_dialog_input_generation() &&
            request.pad_generation == pad_config_get_generation() &&
            displayManager->navigateList(request.navigation.target, request.navigation.command,
                                         request.screen_generation);
    };
    if (displayManager->isInLvglTask()) {
        execute(&request, &ok, nullptr, 0);
    } else {
        const auto result = display_manager_dispatch(execute, nullptr, &request, sizeof(request),
                                                      1000, &ok, nullptr, 0);
        if (result != DISPLAY_TASK_DISPATCH_OK) {
            LOGW("ListNav", "Display dispatch failed (%d)", static_cast<int>(result));
            return ACTION_FAILED;
        }
    }
    if (!ok) LOGW("ListNav", "List navigation rejected for target '%s'", request.navigation.target);
    return ok ? ACTION_COMPLETE : ACTION_FAILED;
#else
    (void)action;
    return ACTION_FAILED;
#endif
}

void describe_list_navigation(JsonObject& action) {
    action["group"] = "Navigation";
    action["label"] = "Navigate List";
    JsonArray choices = action.createNestedArray("commands");
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        JsonObject choice = choices.createNestedObject();
        choice["id"] = commands[i];
        choice["label"] = labels[i];
    }
    JsonArray fields = action.createNestedArray("fields");
    JsonObject target = fields.createNestedObject();
    target["name"] = "target";
    target["description"] = "List ID on the active pad";
    JsonObject command = fields.createNestedObject();
    command["name"] = "command";
    command["description"] = "previous_page or next_page";
    JsonArray editor = action.createNestedArray("editor_fields");
    JsonObject target_editor = editor.createNestedObject();
    target_editor["name"] = "target"; target_editor["label"] = "List ID"; target_editor["type"] = "text";
    JsonObject command_editor = editor.createNestedObject();
    command_editor["name"] = "command"; command_editor["label"] = "Command";
    command_editor["type"] = "select"; command_editor["command_options"] = true;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kListNavigationActionType, ACTION_TYPE_LIST_NAVIGATION,
    parse_list_navigation, serialize_list_navigation, dispatch_list_navigation, nullptr,
    describe_list_navigation, nullptr, validate_list_navigation,
    ACTION_EXECUTION_SYNC, false, true, false);
}
#endif
