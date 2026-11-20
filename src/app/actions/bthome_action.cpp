#include "action_registry.h"
#include "bthome_events.h"
#include "log_manager.h"

#if HAS_DISPLAY || HAS_BUTTON
namespace {
struct BthomeEventOption {
    const char* id;
    const char* label;
    uint8_t value;
};
constexpr BthomeEventOption kBthomeEventOptions[] = {
    {"press", "Press", bthome::press},
    {"double_press", "Double press", bthome::double_press},
    {"long_press", "Long press", bthome::long_press}
};

const BthomeEventOption* bthome_event_option(const char* id) {
    for (const auto& option : kBthomeEventOptions)
        if (strcmp(id, option.id) == 0) return &option;
    return nullptr;
}

ActionResult dispatch_bthome(const BthomePayload& payload, const char* label, uint32_t) {
#if defined(ARDUINO) && (HAS_BLE || HAS_BLE_HID)
    if (bthome_events_send(payload.button, payload.event))
        return ACTION_COMPLETE;
#else
    (void)payload;
#endif
    LOGW("Action", "%s bthome: event not queued", label);
    return ACTION_FAILED;
}

const char* const kBthomeEventChoices[] = {
    kBthomeEventOptions[0].id, kBthomeEventOptions[1].id, kBthomeEventOptions[2].id
};
const char* const kBthomeEventLabels[] = {
    kBthomeEventOptions[0].label, kBthomeEventOptions[1].label, kBthomeEventOptions[2].label
};
const ActionArgumentDef kBthomeSendArguments[] = {
    {"button", "Button number", ACTION_ARGUMENT_INTEGER, 1, bthome::button_count, 0,
     true, "[var:button]", "bthome_buttons"},
    {"event", "Event", ACTION_ARGUMENT_ENUM, 0, 0, 12, true,
     "[var:event]", "select", kBthomeEventChoices, 3, false, nullptr, kBthomeEventLabels},
};
ActionResult dispatch_bthome_send(const ActionResolvedArgument* arguments, const char* label,
                                  uint32_t token) {
    const BthomeEventOption* option = bthome_event_option(arguments[1].text);
    if (!option) return ACTION_FAILED;
    BthomePayload payload = {static_cast<uint8_t>(arguments[0].integer), option->value};
    return dispatch_bthome(payload, label, token);
}
const ActionOperationDef kBthomeOperations[] = {
    {"bthome.send", "Send BTHome event", kBthomeSendArguments, 2, dispatch_bthome_send},
};

bool bthome_available() {
#if HAS_BLE || HAS_BLE_HID
    return true;
#else
    return false;
#endif
}

void describe_bthome(JsonObject& action) {
    action["group"] = "Connectivity";
    action["label"] = "Send BTHome event";
    action["note"] = "Unencrypted Bluetooth broadcast; no delivery acknowledgement. Independent of MQTT. Four waiting events; rejects newest when full.";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kBthomeActionType, ACTION_TYPE_BTHOME, describe_bthome, bthome_available, ACTION_EXECUTION_SYNC, false, false,
    kBthomeOperations, sizeof(kBthomeOperations) / sizeof(kBthomeOperations[0]));
}
#endif
