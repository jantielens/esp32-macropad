#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_BLE_HID
#include "ble_hid.h"
#include "keyboard_hid.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kBlePairActionTag = "Action";
void parse_ble_pair(const JsonObject&, ButtonAction&) {}
void serialize_ble_pair(const ButtonAction&, JsonObject) {}
ActionResult start_ble_pairing(const char* label) {
#if defined(ARDUINO) && HAS_BLE_HID
    if (!ble_hid_is_initialized() || keyboard_hid_transport() != KeyboardTransport::Ble || keyboard_hid_is_busy()) {
        LOGW(kBlePairActionTag, "%s ble_pair: unavailable or busy", label);
        return ACTION_FAILED;
    }
    else { LOGI(kBlePairActionTag, "%s ble_pair: starting re-pairing", label); ble_hid_request_pairing(); }
#else
    LOGW(kBlePairActionTag, "%s ble_pair: not compiled", label);
#endif
    return ACTION_COMPLETE;
}
ActionResult dispatch_ble_pair(const ButtonAction&, const char* label, uint32_t) {
    return start_ble_pairing(label);
}
bool ble_pair_available() { return HAS_BLE_HID; }
const char* validate_ble_pair(JsonObjectConst) { return nullptr; }
void describe_ble_pair(JsonObject& action) { action["group"] = "BLE"; action["label"] = "Start BLE pairing"; }
ActionResult dispatch_ble_pair_operation(const ActionResolvedArgument*, const char* label, uint32_t token) {
#if defined(ARDUINO) && HAS_BLE_HID
    (void)token;
    return start_ble_pairing(label);
#else
    (void)token;
    LOGW(kBlePairActionTag, "%s ble_pair: not compiled", label);
    return ACTION_FAILED;
#endif
}
const ActionOperationDef kBlePairOperations[] = {
    {"ble.pair", "Start BLE pairing", nullptr, 0, dispatch_ble_pair_operation},
};
DEFINE_AND_REGISTER_ACTION_TYPE(kBlePairActionType, ACTION_TYPE_BLE_PAIR, parse_ble_pair, serialize_ble_pair, dispatch_ble_pair, nullptr, describe_ble_pair, ble_pair_available, validate_ble_pair, ACTION_EXECUTION_SYNC, true, true, false, nullptr,
    kBlePairOperations, sizeof(kBlePairOperations) / sizeof(kBlePairOperations[0]));
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON