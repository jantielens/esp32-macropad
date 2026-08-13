#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_AUDIO
#include "audio.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON

namespace {

constexpr const char* kVolumeActionTag = "Action";

void parse_volume(const JsonObject& action, ButtonAction& act) {
    strlcpy(act.payload.volume.volume_mode, action["volume_mode"] | "",
            sizeof(act.payload.volume.volume_mode));
    strlcpy(act.payload.volume.volume_value, action["volume_value"] | "",
            sizeof(act.payload.volume.volume_value));
}

void serialize_volume(const ButtonAction& act, JsonObject action) {
    if (act.payload.volume.volume_mode[0]) action["volume_mode"] = act.payload.volume.volume_mode;
    if (act.payload.volume.volume_value[0]) action["volume_value"] = act.payload.volume.volume_value;
}

ActionResult dispatch_volume(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_AUDIO
    const auto& volume = act.payload.volume;
    const bool adjust = strcmp(volume.volume_mode, "adjust") == 0;
    int value = adjust ? audio_get_volume() + lroundf(atof(volume.volume_value))
                       : lroundf(atof(volume.volume_value));
    value = constrain(value, 0, 100);
    audio_set_volume(value);
    LOGI(kVolumeActionTag, "%s volume %s %s -> %u%%", label, adjust ? "adjust" : "set",
         volume.volume_value, value);
#else
    (void)act;
    LOGW(kVolumeActionTag, "%s volume: not compiled", label);
#endif
    return ACTION_COMPLETE;
}

bool volume_available() {
#if HAS_AUDIO
    return true;
#else
    return false;
#endif
}

const char* validate_volume(const JsonObjectConst action) {
    if (!action.containsKey("volume_mode")) return nullptr;
    if (!action["volume_mode"].is<const char*>()) return "action command must be a string";
    const char* mode = action["volume_mode"].as<const char*>();
    return strcmp(mode, "set") == 0 || strcmp(mode, "adjust") == 0
        ? nullptr : "volume_mode must be set or adjust";
}

bool visit_volume_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) {
    return !act.payload.volume.volume_value[0] || visitor(act.payload.volume.volume_value,
        sizeof(act.payload.volume.volume_value), false, context);
}

void describe_volume(JsonObject& action) {
    action["group"] = "Audio";
    action["label"] = "Volume";
    JsonArray commands = action.createNestedArray("commands");
    JsonObject set = commands.createNestedObject(); set["id"] = "set"; set["label"] = "Set volume";
    JsonObject adjust = commands.createNestedObject(); adjust["id"] = "adjust"; adjust["label"] = "Adjust volume";
    JsonArray fields = action.createNestedArray("fields");
    JsonObject mode = fields.createNestedObject(); mode["name"] = "volume_mode"; mode["description"] = "set or adjust; matches the selected command";
    JsonObject value = fields.createNestedObject(); value["name"] = "volume_value"; value["description"] = "percentage for set; signed delta for adjust";
    JsonArray editor_fields = action.createNestedArray("editor_fields");
    JsonObject editor_mode = editor_fields.createNestedObject(); editor_mode["name"] = "volume_mode"; editor_mode["label"] = "Command"; editor_mode["type"] = "select"; editor_mode["command_options"] = true;
    JsonObject editor_value = editor_fields.createNestedObject(); editor_value["name"] = "volume_value"; editor_value["label"] = "Value (%)"; editor_value["type"] = "text"; editor_value["bindable"] = true;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kVolumeActionType, ACTION_TYPE_VOLUME, parse_volume,
    serialize_volume, dispatch_volume, nullptr, describe_volume, volume_available,
    validate_volume, visit_volume_fields);

} // namespace

#endif // HAS_DISPLAY || HAS_BUTTON