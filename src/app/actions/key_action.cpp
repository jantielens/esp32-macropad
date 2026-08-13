#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_BLE_HID
#include "ble_hid.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kKeyActionTag = "Action";
void parse_key(const JsonObject& action, ButtonAction& act) { strlcpy(act.payload.key.key_sequence, action["sequence"] | "", sizeof(act.payload.key.key_sequence)); }
void serialize_key(const ButtonAction& act, JsonObject action) { if (act.payload.key.key_sequence[0]) action["sequence"] = act.payload.key.key_sequence; }
ActionResult dispatch_key(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_BLE_HID
    if (!ble_hid_is_initialized()) LOGW(kKeyActionTag, "%s key: BLE disabled", label);
    else if (act.payload.key.key_sequence[0]) { LOGI(kKeyActionTag, "%s key: '%s'", label, act.payload.key.key_sequence); ble_hid_request_sequence(act.payload.key.key_sequence); }
    else LOGW(kKeyActionTag, "%s key: empty sequence", label);
#else
    (void)act;
    LOGW(kKeyActionTag, "%s key: not compiled", label);
#endif
    return ACTION_COMPLETE;
}
char* key_value_field(ButtonAction& act, size_t* size) { *size = sizeof(act.payload.key.key_sequence); return act.payload.key.key_sequence; }
bool key_available() { return HAS_BLE_HID; }
const char* validate_key(const JsonObjectConst action) { return action.containsKey("sequence") && !action["sequence"].is<const char*>() ? "key sequence must be a string" : nullptr; }
void describe_key(JsonObject& action) { action["group"] = "BLE"; action["label"] = "Send BLE keys"; JsonArray fields = action.createNestedArray("fields"); JsonObject sequence = fields.createNestedObject(); sequence["name"] = "sequence"; sequence["description"] = "key sequence DSL"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kKeyActionType, ACTION_TYPE_KEY, parse_key, serialize_key, dispatch_key, key_value_field, describe_key, key_available, validate_key);
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON