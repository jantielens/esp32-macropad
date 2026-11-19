#include "action_registry.h"
#include "action_dispatch.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_AUDIO
#include "audio.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kSoundAlertActionTag = "Action";
#if defined(ARDUINO) && HAS_AUDIO
ActionResult dispatch_sound_alert(const ButtonAction& act, const char* label, uint32_t) {
    const auto& alert = act.payload.sound_alert;
    const ActionDispatchContext context = action_dispatch_context();
    bool accepted = true;
    if (!strcmp(alert.sound_alert_kind, "stop_all")) audio_stop_all();
    else if (!strcmp(alert.sound_alert_kind, "stop")) {
        if (context.work_guard) audio_stop_owned(context.work_guard, context.generation);
        else audio_stop();
    }
    else if (!strcmp(alert.sound_alert_kind, "tone") || !strcmp(alert.sound_alert_kind, "tone_loop"))
        accepted = audio_submit_tone(alert.sound_alert_pattern, alert.sound_alert_volume, !strcmp(alert.sound_alert_kind, "tone_loop"), context.work_guard, context.generation, context.alarm_audio);
    else if (!strcmp(alert.sound_alert_kind, "mp3") || !strcmp(alert.sound_alert_kind, "mp3_loop")) {
#if HAS_SOUND_PLAYER
        accepted = audio_submit_sound(alert.sound_alert_file, alert.sound_alert_volume, context.work_guard, context.generation,
            !strcmp(alert.sound_alert_kind, "mp3_loop"), context.alarm_audio);
#else
        LOGW(kSoundAlertActionTag, "%s sound_alert MP3: not compiled", label);
#endif
    } else LOGW(kSoundAlertActionTag, "%s sound_alert: invalid kind", label);
    if (!accepted) {
        return ACTION_FAILED;
    }
    return ACTION_COMPLETE;
}
#endif
const ActionArgumentDef kSoundToneArguments[] = {
    {"pattern", "Tone pattern", ACTION_ARGUMENT_STRING, 0, 0,
     CONFIG_BEEP_PATTERN_MAX_LEN - 1, true, "[var:pattern]", "text", nullptr, 0, true},
    {"volume", "Volume override (%; 0 uses device volume)", ACTION_ARGUMENT_INTEGER, 0, 100, 0,
     true, "[var:volume]", "number"},
};
const ActionArgumentDef kSoundFileArguments[] = {
    {"file", "MP3 file", ACTION_ARGUMENT_STRING, 0, 0,
     sizeof(SoundAlertPayload::sound_alert_file) - 1, true, "[var:sound]", "sounds"},
    {"volume", "Volume override (%; 0 uses device volume)", ACTION_ARGUMENT_INTEGER, 0, 100, 0,
     true, "[var:volume]", "number"},
};

ActionResult run_sound_operation(const ActionResolvedArgument* arguments, const char* label,
                                 uint32_t token, const char* kind, bool file) {
#if defined(ARDUINO) && HAS_AUDIO
    ButtonAction action = {};
    auto& alert = action.payload.sound_alert;
    strlcpy(alert.sound_alert_kind, kind, sizeof(alert.sound_alert_kind));
    if (arguments) {
        if (file) strlcpy(alert.sound_alert_file, arguments[0].text, sizeof(alert.sound_alert_file));
        else strlcpy(alert.sound_alert_pattern, arguments[0].text, sizeof(alert.sound_alert_pattern));
        alert.sound_alert_volume = static_cast<uint8_t>(arguments[1].integer);
    }
    return dispatch_sound_alert(action, label, token);
#else
    (void)arguments; (void)label; (void)token; (void)kind; (void)file;
    return ACTION_FAILED;
#endif
}
ActionResult run_sound_tone(const ActionResolvedArgument* args, const char* label, uint32_t token) {
    return run_sound_operation(args, label, token, "tone", false);
}
#if HAS_SOUND_PLAYER
ActionResult run_sound_mp3(const ActionResolvedArgument* args, const char* label, uint32_t token) {
    return run_sound_operation(args, label, token, "mp3", true);
}
#endif
ActionResult run_sound_stop(const ActionResolvedArgument*, const char* label, uint32_t token) {
    return run_sound_operation(nullptr, label, token, "stop", false);
}
ActionResult run_sound_stop_all(const ActionResolvedArgument*, const char* label, uint32_t token) {
    return run_sound_operation(nullptr, label, token, "stop_all", false);
}
const ActionOperationDef kSoundAlertOperations[] = {
    {"audio.alert.tone", "Play tone alert", kSoundToneArguments, 2, run_sound_tone},
#if HAS_SOUND_PLAYER
    {"audio.alert.mp3", "Play MP3 alert", kSoundFileArguments, 2, run_sound_mp3},
#endif
    {"audio.alert.stop", "Stop audio alerts", nullptr, 0, run_sound_stop},
    {"audio.stop_all", "Stop all audio", nullptr, 0, run_sound_stop_all},
};
bool sound_alert_available() {
#if HAS_AUDIO
    return true;
#else
    return false;
#endif
}
void describe_sound_alert(JsonObject& action) { action["group"] = "Audio"; action["label"] = "Sound alert"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kSoundAlertActionType, ACTION_TYPE_SOUND_ALERT, describe_sound_alert, sound_alert_available, ACTION_EXECUTION_SYNC, true, false,
    kSoundAlertOperations, sizeof(kSoundAlertOperations) / sizeof(kSoundAlertOperations[0]));
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON