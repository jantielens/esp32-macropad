#include "action_registry.h"
#include "action_dispatch.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_AUDIO
#include "audio.h"
#if ALARM_ENABLED
#include "alarm_manager.h"
#endif
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kSoundAlertActionTag = "Action";
const char* validate_sound_alert_payload(const JsonObjectConst action, const char* kind) {
    if (strcmp(kind, "tone") && strcmp(kind, "tone_loop") && strcmp(kind, "mp3") && strcmp(kind, "mp3_loop") && strcmp(kind, "stop")) return "Invalid sound_alert_kind";
    const char* names[] = {"sound_alert_pattern", "sound_alert_file"};
    const size_t capacities[] = {sizeof(SoundAlertPayload::sound_alert_pattern), sizeof(SoundAlertPayload::sound_alert_file)};
    for (uint8_t index = 0; index < 2; ++index)
        if (action.containsKey(names[index]) && (!action[names[index]].is<const char*>() || strlen(action[names[index]].as<const char*>()) >= capacities[index])) return "Sound field is invalid or oversized";
    if (action.containsKey("sound_alert_volume") && (!action["sound_alert_volume"].is<uint8_t>() || action["sound_alert_volume"].as<uint8_t>() > 100)) return "sound_alert_volume must be 0-100";
    const char* pattern = action["sound_alert_pattern"] | "";
    const char* file = action["sound_alert_file"] | "";
    const bool mp3 = !strcmp(kind, "mp3") || !strcmp(kind, "mp3_loop");
    if (mp3 && (pattern[0] || !file[0])) return "MP3 requires a file and no tone pattern";
    if (!mp3 && file[0]) return "Tone and stop actions do not accept a file";
    if (!strcmp(kind, "stop") && pattern[0]) return "Stop does not accept a tone pattern";
    return nullptr;
}
const char* validate_sound_alert(const JsonObjectConst action) {
    return validate_sound_alert_payload(action, action["sound_alert_kind"] | "");
}
void parse_sound_alert_payload(const JsonObject& action, ButtonAction& act, const char* kind) {
    if (validate_sound_alert_payload(action, kind)) { memset(&act, 0, sizeof(act)); return; }
    auto& alert = act.payload.sound_alert;
    strlcpy(alert.sound_alert_kind, kind, sizeof(alert.sound_alert_kind));
    strlcpy(alert.sound_alert_pattern, action["sound_alert_pattern"] | "", sizeof(alert.sound_alert_pattern));
    strlcpy(alert.sound_alert_file, action["sound_alert_file"] | "", sizeof(alert.sound_alert_file));
    alert.sound_alert_volume = action["sound_alert_volume"] | 0;
}
void parse_sound_alert(const JsonObject& action, ButtonAction& act) {
    parse_sound_alert_payload(action, act, action["sound_alert_kind"] | "");
}
void serialize_sound_alert(const ButtonAction& act, JsonObject action) { action["sound_alert_kind"] = act.payload.sound_alert.sound_alert_kind; if ((!strcmp(act.payload.sound_alert.sound_alert_kind, "tone") || !strcmp(act.payload.sound_alert.sound_alert_kind, "tone_loop")) && act.payload.sound_alert.sound_alert_pattern[0]) action["sound_alert_pattern"] = act.payload.sound_alert.sound_alert_pattern; if ((!strcmp(act.payload.sound_alert.sound_alert_kind, "mp3") || !strcmp(act.payload.sound_alert.sound_alert_kind, "mp3_loop")) && act.payload.sound_alert.sound_alert_file[0]) action["sound_alert_file"] = act.payload.sound_alert.sound_alert_file; if (act.payload.sound_alert.sound_alert_volume) action["sound_alert_volume"] = act.payload.sound_alert.sound_alert_volume; }
ActionResult dispatch_sound_alert(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_AUDIO
    const auto& alert = act.payload.sound_alert;
    const ActionDispatchContext context = action_dispatch_context();
    const AudioPlaybackFailure failure =
#if ALARM_ENABLED
        alarm_manager_report_audio_failure;
#else
        nullptr;
#endif
    bool accepted = true;
    if (!strcmp(alert.sound_alert_kind, "stop")) audio_stop();
    else if (!strcmp(alert.sound_alert_kind, "tone") || !strcmp(alert.sound_alert_kind, "tone_loop"))
        accepted = audio_submit_tone(alert.sound_alert_pattern, alert.sound_alert_volume, !strcmp(alert.sound_alert_kind, "tone_loop"), context.work_guard, context.generation, failure);
    else if (!strcmp(alert.sound_alert_kind, "mp3") || !strcmp(alert.sound_alert_kind, "mp3_loop")) {
#if HAS_SOUND_PLAYER
        accepted = audio_submit_sound(alert.sound_alert_file, alert.sound_alert_volume, context.work_guard, context.generation,
            !strcmp(alert.sound_alert_kind, "mp3_loop"), failure);
#else
        LOGW(kSoundAlertActionTag, "%s sound_alert MP3: not compiled", label);
#endif
    } else LOGW(kSoundAlertActionTag, "%s sound_alert: invalid kind", label);
    if (!accepted) {
        if (failure) failure(context.work_guard, context.generation);
        return ACTION_FAILED;
    }
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
bool visit_sound_alert_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) { return (strcmp(act.payload.sound_alert.sound_alert_kind, "tone") && strcmp(act.payload.sound_alert.sound_alert_kind, "tone_loop")) || !act.payload.sound_alert.sound_alert_pattern[0] || visitor(act.payload.sound_alert.sound_alert_pattern, sizeof(act.payload.sound_alert.sound_alert_pattern), false, context); }
void describe_sound_alert(JsonObject& action) { action["group"] = "Audio"; action["label"] = "Sound alert"; JsonArray fields = action.createNestedArray("fields"); JsonObject kind = fields.createNestedObject(); kind["name"] = "sound_alert_kind"; kind["description"] = "tone, tone_loop, mp3, or stop"; }
#if ALARM_ENABLED
void describe_alarm_audio(JsonObject& action, bool mp3) {
    JsonArray fields = action.createNestedArray("fields");
    JsonObject source = fields.createNestedObject();
    source["name"] = mp3 ? "sound_alert_file" : "sound_alert_pattern";
    source["description"] = mp3 ? "MP3 sound name to repeat until stopped" : "Looping tone pattern; supports bindings";
    JsonObject volume = fields.createNestedObject();
    volume["name"] = "sound_alert_volume";
    volume["description"] = "0 uses device volume; 1-100 overrides volume";
    JsonArray editor = action.createNestedArray("editor_fields");
    JsonObject source_editor = editor.createNestedObject();
    source_editor["name"] = mp3 ? "sound_alert_file" : "sound_alert_pattern";
    source_editor["label"] = mp3 ? "MP3 file" : "Tone pattern";
    source_editor["type"] = mp3 ? "select" : "text";
    if (mp3) source_editor["options_source"] = "sounds";
    else source_editor["bindable"] = true;
    JsonObject volume_editor = editor.createNestedObject();
    volume_editor["name"] = "sound_alert_volume";
    volume_editor["label"] = "Volume override (%)";
    volume_editor["type"] = "number";
    volume_editor["min"] = 0;
    volume_editor["max"] = 100;
    volume_editor["default"] = "0";
}
#endif
DEFINE_AND_REGISTER_ACTION_TYPE(kSoundAlertActionType, ACTION_TYPE_SOUND_ALERT, parse_sound_alert, serialize_sound_alert, dispatch_sound_alert, nullptr, describe_sound_alert, sound_alert_available, validate_sound_alert, visit_sound_alert_fields, ACTION_EXECUTION_SYNC);
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON