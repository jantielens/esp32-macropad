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
bool notify_available() { return HAS_DISPLAY; }
const char* validate_notify(JsonObjectConst action) { return dialog_action::validate(action, false); }
bool visit_notify_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) {
    return visitor(act.payload.notify.message, sizeof(act.payload.notify.message), true, context);
}
void describe_notify(JsonObject& action) { dialog_action::describe(action, false); }
DEFINE_AND_REGISTER_ACTION_TYPE(kNotifyActionType, ACTION_TYPE_NOTIFY, parse_notify, serialize_notify,
    dispatch_notify, nullptr, describe_notify, notify_available, validate_notify,
    visit_notify_fields, ACTION_EXECUTION_SYNC);
}
#endif
