#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_BLE_HID
#include "ble_hid.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kBlePairActionTag = "Action";
void parse_ble_pair(const JsonObject&, ButtonAction&) {}
void serialize_ble_pair(const ButtonAction&, JsonObject) {}
ActionResult dispatch_ble_pair(const ButtonAction&, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_BLE_HID
    if (!ble_hid_is_initialized()) LOGW(kBlePairActionTag, "%s ble_pair: BLE disabled", label);
    else { LOGI(kBlePairActionTag, "%s ble_pair: starting re-pairing", label); ble_hid_request_pairing(); }
#else
    LOGW(kBlePairActionTag, "%s ble_pair: not compiled", label);
#endif
    return ACTION_COMPLETE;
}
bool ble_pair_available() { return HAS_BLE_HID; }
const char* validate_ble_pair(JsonObjectConst) { return nullptr; }
void describe_ble_pair(JsonObject& action) { action["group"] = "BLE"; action["label"] = "Start BLE pairing"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kBlePairActionType, ACTION_TYPE_BLE_PAIR, parse_ble_pair, serialize_ble_pair, dispatch_ble_pair, nullptr, describe_ble_pair, ble_pair_available, validate_ble_pair);
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON