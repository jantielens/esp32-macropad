#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO)
#include "keyboard_hid.h"
#endif

#if (HAS_DISPLAY || HAS_BUTTON) && (HAS_BLE_HID || HAS_USB_HID)
namespace {
constexpr const char* kKeyActionTag = "Action";
void parse_key(const JsonObject& action, ButtonAction& act) { strlcpy(act.payload.key.key_sequence, action["sequence"] | "", sizeof(act.payload.key.key_sequence)); }
void serialize_key(const ButtonAction& act, JsonObject action) { if (act.payload.key.key_sequence[0]) action["sequence"] = act.payload.key.key_sequence; }
ActionResult send_key_sequence(const char* sequence, const char* label, uint32_t continuation_token) {
#if defined(ARDUINO)
    if (!keyboard_hid_request_sequence(sequence, continuation_token)) {
        LOGW(kKeyActionTag, "%s key: request rejected (busy, disconnected, empty, or OTA)", label);
        return ACTION_FAILED;
    }
    LOGT(kKeyActionTag, "%s key: transport=%s bytes=%u", label, keyboard_transport_name(keyboard_hid_transport()), unsigned(strlen(sequence)));
    return ACTION_PENDING;
#else
    (void)continuation_token;
    (void)sequence;
    LOGW(kKeyActionTag, "%s key: not compiled", label);
    return ACTION_COMPLETE;
#endif
}
ActionResult dispatch_key(const ButtonAction& act, const char* label, uint32_t continuation_token) {
    return send_key_sequence(act.payload.key.key_sequence, label, continuation_token);
}
ActionResult dispatch_key_operation(const ActionResolvedArgument* arguments, const char* label, uint32_t token) {
#if defined(ARDUINO)
    return send_key_sequence(arguments[0].text, label, token);
#else
    (void)arguments;
    (void)token;
    LOGW(kKeyActionTag, "%s key: not compiled", label);
    return ACTION_FAILED;
#endif
}
const ActionArgumentDef kKeyArguments[] = {
    {"sequence", "Key sequence", ACTION_ARGUMENT_STRING, 0, 0,
     CONFIG_KEY_SEQ_MAX_LEN - 1, true, "[var:keys]", "text"},
};
const ActionOperationDef kKeyOperations[] = {
    {"keyboard.send", "Send keys", kKeyArguments, 1, dispatch_key_operation, ACTION_EXECUTION_PAUSABLE},
};
bool visit_key_fields(ButtonAction& act, ActionFieldVisitor visitor, void* context) {
    return action_visit_field(visitor, context, "sequence", act.payload.key.key_sequence);
}
bool key_available() { return HAS_BLE_HID || HAS_USB_HID; }
const char* validate_key(const JsonObjectConst action) { return action.containsKey("sequence") && !action["sequence"].is<const char*>() ? "key sequence must be a string" : nullptr; }
void describe_key(JsonObject& action) { action["group"] = "Keyboard"; action["label"] = "Send keys"; JsonArray fields = action.createNestedArray("fields"); JsonObject sequence = fields.createNestedObject(); sequence["name"] = "sequence"; sequence["description"] = "key sequence DSL"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kKeyActionType, ACTION_TYPE_KEY, parse_key, serialize_key, dispatch_key, visit_key_fields, describe_key, key_available, validate_key, ACTION_EXECUTION_PAUSABLE, true, true, false, nullptr,
    kKeyOperations, sizeof(kKeyOperations) / sizeof(kKeyOperations[0]));
} // namespace
#endif // (HAS_DISPLAY || HAS_BUTTON) && (HAS_BLE_HID || HAS_USB_HID)