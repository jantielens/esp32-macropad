#include "action_registry.h"
#include "gamepad_hid_state.h"
#include "log_manager.h"
#if defined(ARDUINO) && (HAS_USB_HID || HAS_BLE_HID)
#include "gamepad_hid.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
const char* const gamepad_direction_names[] = {"up", "down", "left", "right"};

int gamepad_name_index(const char* value, const char* const* names, size_t count) {
    for (size_t index = 0; index < count; ++index) if (strcmp(value, names[index]) == 0) return int(index);
    return -1;
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
}

DEFINE_AND_REGISTER_ACTION_TYPE(kGamepadActionType, ACTION_TYPE_GAMEPAD, describe_gamepad, gamepad_available, ACTION_EXECUTION_PAUSABLE, true, false,
    kGamepadOperations, sizeof(kGamepadOperations) / sizeof(kGamepadOperations[0]));
}
#endif