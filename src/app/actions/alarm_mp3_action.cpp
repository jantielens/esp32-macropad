#include "action_registry.h"
#if ALARM_ENABLED && (HAS_DISPLAY || HAS_BUTTON)
namespace {
const char* validate_alarm_mp3(JsonObjectConst action) {
    if (action.containsKey("sound_alert_kind")) return "Loop MP3 has a fixed sound kind";
    return validate_sound_alert_payload(action, "mp3_loop");
}
void parse_alarm_mp3(const JsonObject& action, ButtonAction& output) {
    if (validate_alarm_mp3(action)) { memset(&output, 0, sizeof(output)); return; }
    parse_sound_alert_payload(action, output, "mp3_loop");
}
void serialize_alarm_mp3(const ButtonAction& input, JsonObject action) {
    serialize_sound_alert(input, action);
    action.remove("sound_alert_kind");
}
bool alarm_mp3_available() {
#if HAS_AUDIO && HAS_SOUND_PLAYER
    return true;
#else
    return false;
#endif
}
void describe_alarm_mp3(JsonObject& action) {
    action["group"] = "Alarm";
    action["label"] = "Loop MP3";
    describe_alarm_audio(action, true);
}
ActionResult run_alarm_mp3(const ActionResolvedArgument* args, const char* label, uint32_t token) {
    return run_sound_operation(args, label, token, "mp3_loop", true);
}
const ActionOperationDef kAlarmMp3Operations[] = {
    {"alarm.mp3", "Loop alarm MP3", kSoundFileArguments, 2, run_alarm_mp3},
};
DEFINE_AND_REGISTER_ACTION_TYPE(kAlarmMp3ActionType, ACTION_TYPE_ALARM_MP3, parse_alarm_mp3, serialize_alarm_mp3,
    dispatch_sound_alert, nullptr, describe_alarm_mp3, alarm_mp3_available, validate_alarm_mp3,
    ACTION_EXECUTION_SYNC, true, true, false, nullptr, kAlarmMp3Operations,
    sizeof(kAlarmMp3Operations) / sizeof(kAlarmMp3Operations[0]));
}
#endif