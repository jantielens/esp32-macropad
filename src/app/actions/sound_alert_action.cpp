#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_AUDIO
#include "audio.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kSoundAlertActionTag = "Action";
void parse_sound_alert(const JsonObject& action, ButtonAction& act) { const char* kind = action["sound_alert_kind"] | ""; const char* pattern = action["sound_alert_pattern"] | ""; const char* file = action["sound_alert_file"] | ""; uint8_t volume = action["sound_alert_volume"] | 0; if ((strcmp(kind, "tone") && strcmp(kind, "mp3")) || volume > 100 || (!strcmp(kind, "tone") && file[0]) || (!strcmp(kind, "mp3") && (pattern[0] || !file[0]))) { memset(&act, 0, sizeof(act)); return; } strlcpy(act.payload.sound_alert.sound_alert_kind, kind, sizeof(act.payload.sound_alert.sound_alert_kind)); strlcpy(act.payload.sound_alert.sound_alert_pattern, pattern, sizeof(act.payload.sound_alert.sound_alert_pattern)); strlcpy(act.payload.sound_alert.sound_alert_file, file, sizeof(act.payload.sound_alert.sound_alert_file)); act.payload.sound_alert.sound_alert_volume = volume; }
void serialize_sound_alert(const ButtonAction& act, JsonObject action) { action["sound_alert_kind"] = act.payload.sound_alert.sound_alert_kind; if (!strcmp(act.payload.sound_alert.sound_alert_kind, "tone") && act.payload.sound_alert.sound_alert_pattern[0]) action["sound_alert_pattern"] = act.payload.sound_alert.sound_alert_pattern; if (!strcmp(act.payload.sound_alert.sound_alert_kind, "mp3") && act.payload.sound_alert.sound_alert_file[0]) action["sound_alert_file"] = act.payload.sound_alert.sound_alert_file; if (act.payload.sound_alert.sound_alert_volume) action["sound_alert_volume"] = act.payload.sound_alert.sound_alert_volume; }
ActionResult dispatch_sound_alert(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_AUDIO
    const auto& alert = act.payload.sound_alert;
    if (!strcmp(alert.sound_alert_kind, "tone")) audio_beep(alert.sound_alert_pattern, alert.sound_alert_volume);
    else if (!strcmp(alert.sound_alert_kind, "mp3")) {
#if HAS_SOUND_PLAYER
        audio_play_sound(alert.sound_alert_file, alert.sound_alert_volume);
#else
        LOGW(kSoundAlertActionTag, "%s sound_alert MP3: not compiled", label);
#endif
    } else LOGW(kSoundAlertActionTag, "%s sound_alert: invalid kind", label);
#else
    (void)act;
    LOGW(kSoundAlertActionTag, "%s sound_alert: not compiled", label);
#endif
    return ACTION_COMPLETE;
}
bool sound_alert_available() {
#if HAS_AUDIO
    return true;
#else
    return false;
#endif
}
const char* validate_sound_alert(const JsonObjectConst action) { return action.containsKey("sound_alert_kind") && !action["sound_alert_kind"].is<const char*>() ? "sound_alert_kind must be a string" : nullptr; }
bool visit_sound_alert_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) { return strcmp(act.payload.sound_alert.sound_alert_kind, "tone") || !act.payload.sound_alert.sound_alert_pattern[0] || visitor(act.payload.sound_alert.sound_alert_pattern, sizeof(act.payload.sound_alert.sound_alert_pattern), false, context); }
void describe_sound_alert(JsonObject& action) { action["group"] = "Audio"; action["label"] = "Play sound alert"; JsonArray fields = action.createNestedArray("fields"); JsonObject kind = fields.createNestedObject(); kind["name"] = "sound_alert_kind"; kind["description"] = "tone or mp3"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kSoundAlertActionType, ACTION_TYPE_SOUND_ALERT, parse_sound_alert, serialize_sound_alert, dispatch_sound_alert, nullptr, describe_sound_alert, sound_alert_available, validate_sound_alert, visit_sound_alert_fields);
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON