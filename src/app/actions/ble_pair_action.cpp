#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_BLE_HID
#include "ble_hid.h"
#include "keyboard_hid.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kBlePairActionTag = "Action";
#if defined(ARDUINO) && HAS_BLE_HID
ActionResult start_ble_pairing(const char* label) {
    if (!ble_hid_is_initialized() || keyboard_hid_transport() != KeyboardTransport::Ble || keyboard_hid_is_busy()) {
        LOGW(kBlePairActionTag, "%s ble_pair: unavailable or busy", label);
        return ACTION_FAILED;
    }
    else { LOGI(kBlePairActionTag, "%s ble_pair: starting re-pairing", label); ble_hid_request_pairing(); }
    return ACTION_COMPLETE;
}
#endif
bool ble_pair_available() { return HAS_BLE_HID; }
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
DEFINE_AND_REGISTER_ACTION_TYPE(kBlePairActionType, ACTION_TYPE_BLE_PAIR, describe_ble_pair, ble_pair_available, ACTION_EXECUTION_SYNC, true, false,
    kBlePairOperations, sizeof(kBlePairOperations) / sizeof(kBlePairOperations[0]));
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON