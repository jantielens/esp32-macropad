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

ActionResult dispatch_list_navigation(const ListNavigationPayload& navigation, const char*, uint32_t) {
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
    Request request{navigation, 0, device_dialog_input_generation(),
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
    (void)navigation;
    return ACTION_FAILED;
#endif
}

const char* validate_list_target(const char* target) {
    return list_identifier_valid(target) ? nullptr : "target must be a List ID";
}

const ActionArgumentDef kListNavigationArguments[] = {
    {"target", "List ID", ACTION_ARGUMENT_STRING, 0, 0, sizeof(ListNavigationPayload::target) - 1,
     true, "[var:list_id]", "list_ids", nullptr, 0, false, validate_list_target},
    {"command", "Command", ACTION_ARGUMENT_ENUM, 0, 0, sizeof(ListNavigationPayload::command) - 1,
     true, "[var:command]", "select", commands, 2, false, nullptr, labels},
};

ActionResult dispatch_list_navigation_operation(const ActionResolvedArgument* arguments, const char* label, uint32_t token) {
    ListNavigationPayload navigation = {};
    strlcpy(navigation.target, arguments[0].text, sizeof(navigation.target));
    strlcpy(navigation.command, arguments[1].text, sizeof(navigation.command));
    return dispatch_list_navigation(navigation, label, token);
}

const ActionOperationDef kListNavigationOperations[] = {
    {"list.navigate", "Navigate List", kListNavigationArguments, 2, dispatch_list_navigation_operation},
};

void describe_list_navigation(JsonObject& action) {
    action["group"] = "Navigation";
    action["label"] = "Navigate List";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kListNavigationActionType, ACTION_TYPE_LIST_NAVIGATION, describe_list_navigation, nullptr,
    ACTION_EXECUTION_SYNC, false, false, kListNavigationOperations, 1);
}
#endif
