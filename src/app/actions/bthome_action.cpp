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

const char* validate_bthome(JsonObjectConst action) {
    if (action.containsKey("button") && (!action["button"].is<int>() ||
        action["button"].as<int>() < 1 || action["button"].as<int>() > bthome::button_count))
        return "bthome button must be a whole number from 1 to 10";
    if (action.containsKey("event") && !action["event"].is<const char*>())
        return "bthome event must be a string";
    if (!bthome_event_option(action["event"] | "press"))
        return "bthome event must be press, double_press, or long_press";
    return nullptr;
}

void parse_bthome(const JsonObject& action, ButtonAction& act) {
    if (const char* error = validate_bthome(action)) {
        LOGW("Action", "%s", error);
        memset(&act, 0, sizeof(act));
        return;
    }
    act.payload.bthome.button = action["button"] | 1;
    act.payload.bthome.event = bthome_event_option(action["event"] | "press")->value;
}

void serialize_bthome(const ButtonAction& act, JsonObject action) {
    action["button"] = act.payload.bthome.button;
    for (const auto& option : kBthomeEventOptions) {
        if (option.value == act.payload.bthome.event) {
            action["event"] = option.id;
            return;
        }
    }
    LOGW("Action", "Cannot serialize invalid BTHome event");
}

ActionResult dispatch_bthome(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && (HAS_BLE || HAS_BLE_HID)
    if (bthome_events_send(act.payload.bthome.button, act.payload.bthome.event))
        return ACTION_COMPLETE;
#else
    (void)act;
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
    ButtonAction act = {};
    act.payload.bthome.button = static_cast<uint8_t>(arguments[0].integer);
    act.payload.bthome.event = option->value;
    return dispatch_bthome(act, label, token);
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
    JsonArray fields = action.createNestedArray("fields");
    JsonObject button = fields.createNestedObject();
    button["name"] = "button";
    button["description"] = "Device-wide Button number, 1-10 (default 1); match in Home Assistant";
    button["min"] = 1;
    button["max"] = bthome::button_count;
    JsonObject event = fields.createNestedObject();
    event["name"] = "event";
    event["description"] = "press (default), double_press, or long_press; emitted immediately, not gesture detection";
    JsonArray editor = action.createNestedArray("editor_fields");
    JsonObject editor_button = editor.createNestedObject();
    editor_button["name"] = "button";
    editor_button["label"] = "Button number";
    editor_button["type"] = "select";
    editor_button["numeric"] = true;
    editor_button["min"] = 1;
    editor_button["max"] = bthome::button_count;
    editor_button["default"] = 1;
    editor_button["help"] = "Device-wide number. Match Button 1-10 in Home Assistant.";
    JsonArray buttons = editor_button.createNestedArray("options");
    for (uint8_t number = 1; number <= bthome::button_count; ++number) {
        JsonObject entry = buttons.createNestedObject();
        entry["id"] = number;
        char label[16];
        snprintf(label, sizeof(label), "Button %u", number);
        entry["label"] = label;
    }
    JsonObject editor_event = editor.createNestedObject();
    editor_event["name"] = "event";
    editor_event["label"] = "Event";
    editor_event["type"] = "select";
    editor_event["default"] = "press";
    editor_event["help"] = "Sends the selected event immediately over Bluetooth. Match this button number and event in your Home Assistant automation.";
    JsonArray options = editor_event.createNestedArray("options");
    for (const auto& option : kBthomeEventOptions) {
        JsonObject entry = options.createNestedObject();
        entry["id"] = option.id;
        entry["label"] = option.label;
    }
}

DEFINE_AND_REGISTER_ACTION_TYPE(kBthomeActionType, ACTION_TYPE_BTHOME, parse_bthome,
    serialize_bthome, dispatch_bthome, nullptr, describe_bthome, bthome_available,
    validate_bthome, ACTION_EXECUTION_SYNC, false, true, false, nullptr,
    kBthomeOperations, sizeof(kBthomeOperations) / sizeof(kBthomeOperations[0]));
}
#endif
