#include "board_config.h"

#if HAS_DISPLAY
#include "dialog_action.h"
#include "action_continuation.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_DISPLAY
#include "device_dialog.h"
#endif

namespace {
void parse_confirm(const JsonObject& action, ButtonAction& act) {
    if (const char* error = dialog_action::validate(action, true)) {
        LOGW("Action", "Invalid confirmation: %s", error);
        return;
    }
    auto& payload = act.payload.confirm;
    strlcpy(payload.message, action["message"] | "", sizeof(payload.message));
    strlcpy(payload.approve_text, action["approve_text"] | "Continue", sizeof(payload.approve_text));
    strlcpy(payload.cancel_text, action["cancel_text"] | "Cancel", sizeof(payload.cancel_text));
    payload.timeout_ms = action["timeout_ms"] | 10000U;
}
void serialize_confirm(const ButtonAction& act, JsonObject action) {
    const auto& payload = act.payload.confirm;
    action["message"] = payload.message;
    action["approve_text"] = payload.approve_text;
    action["cancel_text"] = payload.cancel_text;
    action["timeout_ms"] = payload.timeout_ms;
}
ActionResult dispatch_confirm(const ButtonAction& act, const char* label, uint32_t token) {
#if defined(ARDUINO) && HAS_DISPLAY
    if (token && dialog_action::timing_valid(act.payload.confirm.timeout_ms) &&
        action_continuation_set_timeout(token, 0) &&
        device_dialog_confirm(act.payload.confirm, token)) return ACTION_PENDING;
#else
    (void)act;
    (void)token;
#endif
    LOGW("Action", "%s confirmation unavailable; remaining actions discarded", label);
    return ACTION_FAILED;
}
const ActionArgumentDef kConfirmArguments[] = {
    {"message", "Message", ACTION_ARGUMENT_STRING, 0, 0, CONFIG_DIALOG_MESSAGE_MAX_LEN - 1,
     true, "[var:message]", "multiline"},
    {"approve_text", "Approve button", ACTION_ARGUMENT_STRING, 0, 0, CONFIG_DIALOG_CAPTION_MAX_LEN - 1,
     true, "[var:approve]", "text", nullptr, 0, false, nullptr, nullptr, 0, "Continue"},
    {"cancel_text", "Cancel button", ACTION_ARGUMENT_STRING, 0, 0, CONFIG_DIALOG_CAPTION_MAX_LEN - 1,
     true, "[var:cancel]", "text", nullptr, 0, false, nullptr, nullptr, 0, "Cancel"},
    {"timeout_ms", "Timeout (ms; 0 = no timeout)", ACTION_ARGUMENT_INTEGER, 0, 300000,
     0, true, "[var:timeout_ms]", "timing", nullptr, 0, false, nullptr, nullptr,
     1000, nullptr, 10000, true, "No timeout"},
};
ActionResult dispatch_confirm_operation(const ActionResolvedArgument* arguments, const char* label,
                                        uint32_t token) {
    if (!dialog_action::timing_valid(static_cast<uint32_t>(arguments[3].integer))) {
        LOGW("Action", "%s confirmation timeout must be 0 or 1000-300000 ms", label);
        return ACTION_FAILED;
    }
    ButtonAction act = {};
    auto& payload = act.payload.confirm;
    strlcpy(payload.message, arguments[0].text, sizeof(payload.message));
    strlcpy(payload.approve_text, arguments[1].text, sizeof(payload.approve_text));
    strlcpy(payload.cancel_text, arguments[2].text, sizeof(payload.cancel_text));
    payload.timeout_ms = static_cast<uint32_t>(arguments[3].integer);
    return dispatch_confirm(act, label, token);
}
const ActionOperationDef kConfirmOperations[] = {
    {"dialog.confirm", "Ask for confirmation", kConfirmArguments, 4,
     dispatch_confirm_operation, ACTION_EXECUTION_PAUSABLE},
};
bool confirm_available() {
#if HAS_DISPLAY && (HAS_TOUCH || HAS_BUTTON)
    return true;
#else
    return false;
#endif
}
const char* validate_confirm(JsonObjectConst action) { return dialog_action::validate(action, true); }
bool visit_confirm_fields(ButtonAction& act, ActionFieldVisitor visitor, void* context) {
    return action_visit_field(visitor, context, "message", act.payload.confirm.message);
}
void describe_confirm(JsonObject& action) { dialog_action::describe(action, true); }
DEFINE_AND_REGISTER_ACTION_TYPE(kConfirmActionType, ACTION_TYPE_CONFIRM, parse_confirm, serialize_confirm,
    dispatch_confirm, visit_confirm_fields, describe_confirm, confirm_available, validate_confirm,
    ACTION_EXECUTION_PAUSABLE, true, true, false, nullptr, kConfirmOperations, 1);
}
#endif
