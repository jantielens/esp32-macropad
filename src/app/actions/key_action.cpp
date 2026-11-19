#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO)
#include "keyboard_hid.h"
#endif

#if (HAS_DISPLAY || HAS_BUTTON) && (HAS_BLE_HID || HAS_USB_HID)
namespace {
constexpr const char* kKeyActionTag = "Action";
#if defined(ARDUINO)
ActionResult send_key_sequence(const char* sequence, const char* label, uint32_t continuation_token) {
    if (!keyboard_hid_request_sequence(sequence, continuation_token)) {
        LOGW(kKeyActionTag, "%s key: request rejected (busy, disconnected, empty, or OTA)", label);
        return ACTION_FAILED;
    }
    LOGT(kKeyActionTag, "%s key: transport=%s bytes=%u", label, keyboard_transport_name(keyboard_hid_transport()), unsigned(strlen(sequence)));
    return ACTION_PENDING;
}
#endif
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
bool key_available() { return HAS_BLE_HID || HAS_USB_HID; }
void describe_key(JsonObject& action) { action["group"] = "Keyboard"; action["label"] = "Send keys"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kKeyActionType, ACTION_TYPE_KEY, describe_key, key_available, ACTION_EXECUTION_PAUSABLE, true, false,
    kKeyOperations, sizeof(kKeyOperations) / sizeof(kKeyOperations[0]));
} // namespace
#endif // (HAS_DISPLAY || HAS_BUTTON) && (HAS_BLE_HID || HAS_USB_HID)