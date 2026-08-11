#include "action_registry.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_dispatch.h"
#include "action_parse.h"

namespace {

void add_editor_field(JsonObject& action, const char* name, const char* label,
                      const char* type, bool bindable = false,
                      bool command_options = false) {
    JsonArray fields = action["editor_fields"].is<JsonArray>()
        ? action["editor_fields"].as<JsonArray>()
        : action.createNestedArray("editor_fields");
    JsonObject field = fields.createNestedObject();
    field["name"] = name;
    field["label"] = label;
    field["type"] = type;
    if (bindable) field["bindable"] = true;
    if (command_options) field["command_options"] = true;
}

void add_field_doc(JsonObject& action, const char* name, const char* description) {
    JsonArray fields = action["fields"].is<JsonArray>()
        ? action["fields"].as<JsonArray>()
        : action.createNestedArray("fields");
    JsonObject field = fields.createNestedObject();
    field["name"] = name;
    field["description"] = description;
}

const char* validate_command(JsonObjectConst action, const char* field,
                             const char* action_name) {
    if (!action.containsKey(field)) return nullptr;
    if (!action[field].is<const char*>()) return "action command must be a string";
    const char* command = action[field].as<const char*>();
    if (strcmp(command, "set") == 0 || strcmp(command, "adjust") == 0) return nullptr;
    return action_name;
}

const char* validate_volume(JsonObjectConst action) {
    return validate_command(action, "volume_mode", "volume_mode must be set or adjust");
}

const char* validate_brightness(JsonObjectConst action) {
    return validate_command(action, "brightness_mode", "brightness_mode must be set or adjust");
}

void describe_back(JsonObject& action) {
    action["group"] = "Navigation";
    action["label"] = "Navigate back";
}

void describe_volume(JsonObject& action) {
    action["group"] = "Audio";
    action["label"] = "Volume";
    JsonArray commands = action.createNestedArray("commands");
    JsonObject set = commands.createNestedObject();
    set["id"] = "set";
    set["label"] = "Set volume";
    JsonObject adjust = commands.createNestedObject();
    adjust["id"] = "adjust";
    adjust["label"] = "Adjust volume";
    add_field_doc(action, "volume_mode", "set or adjust; matches the selected command");
    add_field_doc(action, "volume_value", "percentage for set; signed delta for adjust");
    add_editor_field(action, "volume_mode", "Command", "select", false, true);
    add_editor_field(action, "volume_value", "Value (%)", "text", true);
}

void describe_brightness(JsonObject& action) {
    action["group"] = "Display";
    action["label"] = "Brightness";
    JsonArray commands = action.createNestedArray("commands");
    JsonObject set = commands.createNestedObject();
    set["id"] = "set";
    set["label"] = "Set brightness";
    JsonObject adjust = commands.createNestedObject();
    adjust["id"] = "adjust";
    adjust["label"] = "Adjust brightness";
    add_field_doc(action, "brightness_mode", "set or adjust; matches the selected command");
    add_field_doc(action, "brightness_value", "percentage for set; signed delta for adjust");
    add_editor_field(action, "brightness_mode", "Command", "select", false, true);
    add_editor_field(action, "brightness_value", "Value (%)", "text", true);
}

const ActionTypeDef kBackActionType = {
    ACTION_TYPE_BACK, nullptr, nullptr, action_dispatch_back, nullptr, describe_back,
};

#if HAS_AUDIO
const ActionTypeDef kVolumeActionType = {
    ACTION_TYPE_VOLUME, action_parse_volume, action_serialize_volume,
    action_dispatch_volume, nullptr, describe_volume, nullptr, validate_volume,
};
#endif

#if HAS_DISPLAY
const ActionTypeDef kBrightnessActionType = {
    ACTION_TYPE_BRIGHTNESS, action_parse_brightness, action_serialize_brightness,
    action_dispatch_brightness, nullptr, describe_brightness, nullptr, validate_brightness,
};
#endif

REGISTER_ACTION_TYPE(kBackActionType);
#if HAS_AUDIO
REGISTER_ACTION_TYPE(kVolumeActionType);
#endif
#if HAS_DISPLAY
REGISTER_ACTION_TYPE(kBrightnessActionType);
#endif

} // namespace

#endif // HAS_DISPLAY || HAS_BUTTON