#include "board_config.h"

#if HAS_DISPLAY
#include "dialog_action.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_DISPLAY
#include "device_dialog.h"
#endif

namespace {
void parse_notify(const JsonObject& action, ButtonAction& act) {
    if (const char* error = dialog_action::validate(action, false)) {
        LOGW("Action", "Invalid notification: %s", error);
        return;
    }
    strlcpy(act.payload.notify.message, action["message"] | "", sizeof(act.payload.notify.message));
    act.payload.notify.duration_ms = action["duration_ms"] | 3000U;
}
void serialize_notify(const ButtonAction& act, JsonObject action) {
    action["message"] = act.payload.notify.message;
    action["duration_ms"] = act.payload.notify.duration_ms;
}
ActionResult dispatch_notify(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_DISPLAY
    if (dialog_action::timing_valid(act.payload.notify.duration_ms) &&
        device_dialog_notify(act.payload.notify.message, act.payload.notify.duration_ms)) return ACTION_COMPLETE;
#else
    (void)act;
#endif
    LOGW("Action", "%s notification could not be displayed", label);
    return ACTION_FAILED;
}
const ActionArgumentDef kNotifyArguments[] = {
    {"message", "Message", ACTION_ARGUMENT_STRING, 0, 0, CONFIG_DIALOG_MESSAGE_MAX_LEN - 1,
     true, "[var:message]", "multiline"},
    {"duration_ms", "Duration (ms; 0 = until dismissed)", ACTION_ARGUMENT_INTEGER, 0, 300000,
     0, true, "[var:duration_ms]", "timing", nullptr, 0, false, nullptr, nullptr,
     1000, nullptr, 3000, true, "Until dismissed"},
};
ActionResult dispatch_notify_operation(const ActionResolvedArgument* arguments, const char* label,
                                       uint32_t token) {
    if (!dialog_action::timing_valid(static_cast<uint32_t>(arguments[1].integer))) {
        LOGW("Action", "%s notification duration must be 0 or 1000-300000 ms", label);
        return ACTION_FAILED;
    }
    ButtonAction act = {};
    strlcpy(act.payload.notify.message, arguments[0].text, sizeof(act.payload.notify.message));
    act.payload.notify.duration_ms = static_cast<uint32_t>(arguments[1].integer);
    return dispatch_notify(act, label, token);
}
const ActionOperationDef kNotifyOperations[] = {
    {"dialog.notify", "Show notification", kNotifyArguments, 2, dispatch_notify_operation},
};
bool notify_available() { return HAS_DISPLAY; }
const char* validate_notify(JsonObjectConst action) { return dialog_action::validate(action, false); }
bool visit_notify_fields(ButtonAction& act, ActionFieldVisitor visitor, void* context) {
    return action_visit_field(visitor, context, "message", act.payload.notify.message);
}
void describe_notify(JsonObject& action) { dialog_action::describe(action, false); }
DEFINE_AND_REGISTER_ACTION_TYPE(kNotifyActionType, ACTION_TYPE_NOTIFY, parse_notify, serialize_notify,
    dispatch_notify, visit_notify_fields, describe_notify, notify_available, validate_notify,
    ACTION_EXECUTION_SYNC, true, true, false, nullptr, kNotifyOperations, 1);
}
#endif
