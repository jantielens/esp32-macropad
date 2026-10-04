#include "action_registry.h"
#include "log_manager.h"
#include "mouse_hid_buttons.h"
#if defined(ARDUINO) && HAS_USB_HID
#include "mouse_hid.h"
#include "usb_hid.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {

uint8_t mouse_button_mask(const char* button) {
    if (strcmp(button, "left") == 0) return mouse_hid_buttons::left;
    if (strcmp(button, "right") == 0) return mouse_hid_buttons::right;
    if (strcmp(button, "middle") == 0) return mouse_hid_buttons::middle;
    return 0;
}

const char* validate_mouse_button(JsonObjectConst action) {
    if (!action.containsKey("button")) return nullptr;
    if (!action["button"].is<const char*>()) return "mouse button must be a string";
    return mouse_button_mask(action["button"].as<const char*>())
        ? nullptr : "mouse button must be left, right, or middle";
}

void parse_mouse_button(const JsonObject& action, ButtonAction& act) {
    if (validate_mouse_button(action)) {
        memset(&act, 0, sizeof(act));
        return;
    }
    strlcpy(act.payload.mouse_button.button, action["button"] | "left",
            sizeof(act.payload.mouse_button.button));
}

void serialize_mouse_button(const ButtonAction& act, JsonObject action) {
    action["button"] = act.payload.mouse_button.button;
}

ActionResult dispatch_mouse_button(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_USB_HID
    if (mouse_hid_click(usb_hid_epoch(), mouse_button_mask(act.payload.mouse_button.button))) {
        return ACTION_COMPLETE;
    }
    LOGW("Action", "%s mouse button: request rejected (disconnected, owned button, queue full, invalid, or OTA)", label);
#else
    (void)act;
    LOGW("Action", "%s mouse button: USB HID not compiled", label);
#endif
    return ACTION_FAILED;
}

bool mouse_button_available() {
    return HAS_USB_HID;
}

void describe_mouse_button(JsonObject& action) {
    action["group"] = "Mouse";
    action["label"] = "Mouse button";
    JsonObject field = action.createNestedArray("fields").createNestedObject();
    field["name"] = "button";
    field["description"] = "left (default), right, or middle; complete USB click; owned drag buttons reject clicks, other-button clicks preserve the drag";
    const char* values[] = { "left", "right", "middle" };
    const char* labels[] = { "Left", "Right", "Middle" };
    JsonArray commands = action.createNestedArray("commands");
    for (size_t index = 0; index < 3; ++index) {
        JsonObject command = commands.createNestedObject();
        command["id"] = values[index];
        command["label"] = labels[index];
    }
    JsonObject editor = action.createNestedArray("editor_fields").createNestedObject();
    editor["name"] = "button";
    editor["label"] = "Button";
    editor["type"] = "select";
    editor["default"] = "left";
    editor["command_options"] = true;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kMouseButtonActionType,
    ACTION_TYPE_MOUSE_BUTTON, parse_mouse_button, serialize_mouse_button,
    dispatch_mouse_button, nullptr, describe_mouse_button,
    mouse_button_available, validate_mouse_button);

}
#endif