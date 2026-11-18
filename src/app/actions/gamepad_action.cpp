#include "action_registry.h"
#include "gamepad_hid_state.h"
#include "log_manager.h"
#if defined(ARDUINO) && (HAS_USB_HID || HAS_BLE_HID)
#include "gamepad_hid.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
const char* const gamepad_control_names[] = {"button", "hat", "trigger"};
const char* const gamepad_direction_names[] = {"up", "down", "left", "right"};
const char* const gamepad_operation_names[] = {"tap", "down", "up"};

int gamepad_name_index(const char* value, const char* const* names, size_t count) {
    for (size_t index = 0; index < count; ++index) if (strcmp(value, names[index]) == 0) return int(index);
    return -1;
}

const char* validate_gamepad(JsonObjectConst action) {
    if (action.containsKey("control") && !action["control"].is<const char*>()) return "gamepad control must be a string";
    const int control = gamepad_name_index(action["control"] | "button", gamepad_control_names, 3);
    if (control < 0) return "gamepad control must be button, hat, or trigger";
    if (action.containsKey("operation") && !action["operation"].is<const char*>()) return "gamepad operation must be a string";
    if (gamepad_name_index(action["operation"] | "tap", gamepad_operation_names, 3) < 0)
        return "gamepad operation must be tap, down, or up";
    if ((control != 0 && action.containsKey("button")) ||
        (control != 1 && action.containsKey("direction")) ||
        (control != 2 && action.containsKey("trigger"))) return "gamepad action has fields for another control family";
    if (control == 0 && action.containsKey("button") &&
        (!action["button"].is<int>() || action["button"].as<int>() < 1 || action["button"].as<int>() > gamepad_protocol::button_count))
        return "gamepad button must be a whole number within the supported button range";
    if (control == 1 && (!action["direction"].is<const char*>() ||
        gamepad_name_index(action["direction"].as<const char*>(), gamepad_direction_names, gamepad_protocol::hat_control_count) < 0))
        return "gamepad hat direction must be up, down, left, or right";
    if (control == 2 && (!action["trigger"].is<const char*>() ||
        (strcmp(action["trigger"].as<const char*>(), "left") != 0 && strcmp(action["trigger"].as<const char*>(), "right") != 0)))
        return "gamepad trigger must be left or right";
    return nullptr;
}

void parse_gamepad(const JsonObject& action, ButtonAction& act) {
    if (validate_gamepad(action)) { memset(&act, 0, sizeof(act)); return; }
    GamepadPayload& payload = act.payload.gamepad;
    payload.control = uint8_t(gamepad_name_index(action["control"] | "button", gamepad_control_names, 3));
    payload.operation = uint8_t(gamepad_name_index(action["operation"] | "tap", gamepad_operation_names, 3));
    if (payload.control == 0) payload.index = uint8_t((action["button"] | 1) - 1);
    else if (payload.control == 1) payload.index = uint8_t(gamepad_name_index(action["direction"] | "", gamepad_direction_names, gamepad_protocol::hat_control_count));
    else payload.index = strcmp(action["trigger"] | "", "right") == 0 ? 1 : 0;
}

void serialize_gamepad(const ButtonAction& act, JsonObject action) {
    const GamepadPayload& payload = act.payload.gamepad;
    if (payload.control > 2 || payload.operation > 2) return;
    action["control"] = gamepad_control_names[payload.control];
    action["operation"] = gamepad_operation_names[payload.operation];
    if (payload.control == 0) action["button"] = payload.index + 1;
    else if (payload.control == 1 && payload.index < gamepad_protocol::hat_control_count) action["direction"] = gamepad_direction_names[payload.index];
    else if (payload.control == 2) action["trigger"] = payload.index ? "right" : "left";
}

ActionResult execute_gamepad(uint8_t kind, uint8_t index, uint8_t operation,
                             const char* label, uint32_t token) {
#if defined(ARDUINO) && (HAS_USB_HID || HAS_BLE_HID)
    const GamepadControl control{static_cast<GamepadControlKind>(kind), index};
    if (operation == 0) {
        if (token && gamepad_hid_tap(control, token)) return ACTION_PENDING;
    } else if (operation <= 2 && gamepad_hid_hold(control, operation == 1)) {
        return ACTION_COMPLETE;
    }
#else
    (void)kind;
    (void)index;
    (void)operation;
    (void)token;
#endif
    LOGW("Action", "%s gamepad: rejected (HID unavailable, invalid, held target, or capacity exhausted)", label);
    return ACTION_FAILED;
}

ActionResult dispatch_gamepad(const ButtonAction& act, const char* label, uint32_t token) {
    const GamepadPayload& payload = act.payload.gamepad;
    return execute_gamepad(payload.control, payload.index, payload.operation, label, token);
}

const char* const gamepad_direction_labels[] = {"Up", "Down", "Left", "Right"};
const char* const gamepad_trigger_names[] = {"left", "right"};
const char* const gamepad_trigger_labels[] = {"Left", "Right"};
const ActionArgumentDef kGamepadButtonArguments[] = {
    {"button", "Button", ACTION_ARGUMENT_INTEGER, 1, gamepad_protocol::button_count, 0,
     true, "[var:gamepad_button]", "gamepad_buttons"},
};
const ActionArgumentDef kGamepadHatArguments[] = {
    {"direction", "Direction", ACTION_ARGUMENT_ENUM, 0, 0, 5, true,
     "[var:gamepad_direction]", "select", gamepad_direction_names, 4, false, nullptr,
     gamepad_direction_labels},
};
const ActionArgumentDef kGamepadTriggerArguments[] = {
    {"trigger", "Trigger", ACTION_ARGUMENT_ENUM, 0, 0, 5, true,
     "[var:gamepad_trigger]", "select", gamepad_trigger_names, 2, false, nullptr,
     gamepad_trigger_labels},
};

template<uint8_t kind, uint8_t operation>
ActionResult dispatch_gamepad_operation(const ActionResolvedArgument* arguments, const char* label,
                                        uint32_t token) {
    uint8_t index;
    if (kind == 0) index = uint8_t(arguments[0].integer - 1);
    else if (kind == 1) index = uint8_t(gamepad_name_index(arguments[0].text, gamepad_direction_names, 4));
    else index = uint8_t(gamepad_name_index(arguments[0].text, gamepad_trigger_names, 2));
    return execute_gamepad(kind, index, operation, label, token);
}

const ActionOperationDef kGamepadOperations[] = {
    {"gamepad.button.tap", "Tap gamepad button", kGamepadButtonArguments, 1, dispatch_gamepad_operation<0, 0>, ACTION_EXECUTION_PAUSABLE},
    {"gamepad.button.down", "Hold gamepad button", kGamepadButtonArguments, 1, dispatch_gamepad_operation<0, 1>},
    {"gamepad.button.up", "Release gamepad button", kGamepadButtonArguments, 1, dispatch_gamepad_operation<0, 2>},
    {"gamepad.hat.tap", "Tap gamepad hat direction", kGamepadHatArguments, 1, dispatch_gamepad_operation<1, 0>, ACTION_EXECUTION_PAUSABLE},
    {"gamepad.hat.down", "Hold gamepad hat direction", kGamepadHatArguments, 1, dispatch_gamepad_operation<1, 1>},
    {"gamepad.hat.up", "Release gamepad hat direction", kGamepadHatArguments, 1, dispatch_gamepad_operation<1, 2>},
    {"gamepad.trigger.tap", "Tap gamepad trigger", kGamepadTriggerArguments, 1, dispatch_gamepad_operation<2, 0>, ACTION_EXECUTION_PAUSABLE},
    {"gamepad.trigger.down", "Hold gamepad trigger", kGamepadTriggerArguments, 1, dispatch_gamepad_operation<2, 1>},
    {"gamepad.trigger.up", "Release gamepad trigger", kGamepadTriggerArguments, 1, dispatch_gamepad_operation<2, 2>},
};

bool gamepad_available() { return HAS_USB_HID || HAS_BLE_HID; }

void describe_gamepad(JsonObject& action) {
    action["group"] = "Gamepad";
    action["label"] = "Gamepad";
    action["button_count"] = gamepad_protocol::button_count;
    action["note"] = "USB or BLE HID transport required. tap waits for submitted press, 50 ms hold, and submitted release; held targets reject taps. down/up share one standalone latch per control, independent of widget holds. Pad exit, replacement, disconnect, and OTA clear holds.";
    JsonArray fields = action.createNestedArray("fields");
    const char* names[] = {"control", "button", "direction", "trigger", "operation"};
    const char* descriptions[] = {"button (default), hat, trigger", "Button number; button controls only (default 1)",
        "up, down, left, right; hat controls only", "left, right; trigger controls only", "tap (default), down, up"};
    for (size_t index = 0; index < 5; ++index) {
        JsonObject field = fields.createNestedObject();
        field["name"] = names[index];
        field["description"] = descriptions[index];
        if (index == 1) {
            field["min"] = 1;
            field["max"] = gamepad_protocol::button_count;
        }
    }
}

DEFINE_AND_REGISTER_ACTION_TYPE(kGamepadActionType, ACTION_TYPE_GAMEPAD, parse_gamepad, serialize_gamepad, dispatch_gamepad, nullptr, describe_gamepad, gamepad_available, validate_gamepad, ACTION_EXECUTION_PAUSABLE, true, true, false, nullptr,
    kGamepadOperations, sizeof(kGamepadOperations) / sizeof(kGamepadOperations[0]));
}
#endif