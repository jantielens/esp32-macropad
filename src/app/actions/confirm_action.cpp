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
bool confirm_available() {
#if HAS_DISPLAY && (HAS_TOUCH || HAS_BUTTON)
    return true;
#else
    return false;
#endif
}
const char* validate_confirm(JsonObjectConst action) { return dialog_action::validate(action, true); }
bool visit_confirm_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) {
    return visitor(act.payload.confirm.message, sizeof(act.payload.confirm.message), true, context);
}
void describe_confirm(JsonObject& action) { dialog_action::describe(action, true); }
DEFINE_AND_REGISTER_ACTION_TYPE(kConfirmActionType, ACTION_TYPE_CONFIRM, parse_confirm, serialize_confirm,
    dispatch_confirm, nullptr, describe_confirm, confirm_available, validate_confirm,
    visit_confirm_fields, ACTION_EXECUTION_PAUSABLE);
}
#endif
