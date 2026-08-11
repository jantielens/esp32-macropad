#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_DISPLAY
#include "display_manager.h"
#include "screen_saver_manager.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON

namespace {

constexpr const char* kBrightnessActionTag = "Action";

void parse_brightness(const JsonObject& action, ButtonAction& act) {
    strlcpy(act.payload.brightness.brightness_mode, action["brightness_mode"] | "",
            sizeof(act.payload.brightness.brightness_mode));
    strlcpy(act.payload.brightness.brightness_value, action["brightness_value"] | "",
            sizeof(act.payload.brightness.brightness_value));
}

void serialize_brightness(const ButtonAction& act, JsonObject action) {
    if (act.payload.brightness.brightness_mode[0]) action["brightness_mode"] = act.payload.brightness.brightness_mode;
    if (act.payload.brightness.brightness_value[0]) action["brightness_value"] = act.payload.brightness.brightness_value;
}

ActionResult dispatch_brightness(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_DISPLAY
    const auto& brightness = act.payload.brightness;
    const bool adjust = strcmp(brightness.brightness_mode, "adjust") == 0;
    int value = adjust ? display_manager_get_backlight_brightness() + lroundf(atof(brightness.brightness_value))
                       : lroundf(atof(brightness.brightness_value));
    value = constrain(value, MIN_USER_BRIGHTNESS, 100);
    screen_saver_manager_set_brightness(value);
    LOGI(kBrightnessActionTag, "%s brightness %s %s -> %u%%", label,
         adjust ? "adjust" : "set", brightness.brightness_value, value);
#else
    (void)act;
    LOGW(kBrightnessActionTag, "%s brightness: no display", label);
#endif
    return ACTION_COMPLETE;
}

bool brightness_available() { return HAS_DISPLAY; }

const char* validate_brightness(const JsonObjectConst action) {
    if (!action.containsKey("brightness_mode")) return nullptr;
    if (!action["brightness_mode"].is<const char*>()) return "action command must be a string";
    const char* mode = action["brightness_mode"].as<const char*>();
    return strcmp(mode, "set") == 0 || strcmp(mode, "adjust") == 0
        ? nullptr : "brightness_mode must be set or adjust";
}

bool visit_brightness_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) {
    return !act.payload.brightness.brightness_value[0] || visitor(act.payload.brightness.brightness_value,
        sizeof(act.payload.brightness.brightness_value), false, context);
}

void describe_brightness(JsonObject& action) {
    action["group"] = "Display"; action["label"] = "Brightness";
    JsonArray commands = action.createNestedArray("commands");
    JsonObject set = commands.createNestedObject(); set["id"] = "set"; set["label"] = "Set brightness";
    JsonObject adjust = commands.createNestedObject(); adjust["id"] = "adjust"; adjust["label"] = "Adjust brightness";
    JsonArray fields = action.createNestedArray("fields");
    JsonObject mode = fields.createNestedObject(); mode["name"] = "brightness_mode"; mode["description"] = "set or adjust; matches the selected command";
    JsonObject value = fields.createNestedObject(); value["name"] = "brightness_value"; value["description"] = "percentage for set; signed delta for adjust";
    JsonArray editor_fields = action.createNestedArray("editor_fields");
    JsonObject editor_mode = editor_fields.createNestedObject(); editor_mode["name"] = "brightness_mode"; editor_mode["label"] = "Command"; editor_mode["type"] = "select"; editor_mode["command_options"] = true;
    JsonObject editor_value = editor_fields.createNestedObject(); editor_value["name"] = "brightness_value"; editor_value["label"] = "Value (%)"; editor_value["type"] = "text"; editor_value["bindable"] = true;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kBrightnessActionType, ACTION_TYPE_BRIGHTNESS,
    parse_brightness, serialize_brightness, dispatch_brightness, nullptr,
    describe_brightness, brightness_available, validate_brightness, visit_brightness_fields);

} // namespace

#endif // HAS_DISPLAY || HAS_BUTTON