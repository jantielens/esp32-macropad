#include "action_registry.h"
#include "log_manager.h"
#include "mouse_hid_buttons.h"
#if defined(ARDUINO) && (HAS_USB_HID || HAS_BLE_HID)
#include "mouse_hid.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {

#if defined(ARDUINO) && (HAS_USB_HID || HAS_BLE_HID)
uint8_t mouse_button_mask(const char* button) {
    if (strcmp(button, "left") == 0) return mouse_hid_buttons::left;
    if (strcmp(button, "right") == 0) return mouse_hid_buttons::right;
    if (strcmp(button, "middle") == 0) return mouse_hid_buttons::middle;
    return 0;
}
#endif

ActionResult click_mouse_button(const char* button, const char* label) {
#if defined(ARDUINO) && (HAS_USB_HID || HAS_BLE_HID)
    if (mouse_hid_click(mouse_hid_epoch(), mouse_button_mask(button))) {
        return ACTION_COMPLETE;
    }
    LOGW("Action", "%s mouse button: request rejected (disconnected, owned button, queue full, invalid, or OTA)", label);
#else
    (void)button;
    LOGW("Action", "%s mouse button: HID not compiled", label);
#endif
    return ACTION_FAILED;
}

ActionResult dispatch_mouse_click(const ActionResolvedArgument* arguments, const char* label, uint32_t) {
    return click_mouse_button(arguments[0].text, label);
}
const char* const kMouseButtons[] = {"left", "right", "middle"};
const char* const kMouseButtonLabels[] = {"Left", "Right", "Middle"};
const ActionArgumentDef kMouseClickArguments[] = {
    {"button", "Button", ACTION_ARGUMENT_ENUM, 0, 0, 6, true, "[var:mouse_button]",
     "select", kMouseButtons, 3, false, nullptr, kMouseButtonLabels},
};
const ActionOperationDef kMouseOperations[] = {
    {"mouse.click", "Click mouse button", kMouseClickArguments, 1, dispatch_mouse_click},
};

bool mouse_button_available() {
    return HAS_USB_HID || HAS_BLE_HID;
}

void describe_mouse_button(JsonObject& action) {
    action["group"] = "Mouse";
    action["label"] = "Mouse button";
    action["note"] = "A click on a drag-owned button is rejected; clicks on other buttons preserve the drag.";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kMouseButtonActionType, ACTION_TYPE_MOUSE_BUTTON, describe_mouse_button, mouse_button_available, ACTION_EXECUTION_SYNC, true, false,
    kMouseOperations, sizeof(kMouseOperations) / sizeof(kMouseOperations[0]));

}
#endif