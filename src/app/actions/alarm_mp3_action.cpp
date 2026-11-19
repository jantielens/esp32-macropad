#include "action_registry.h"
#if ALARM_ENABLED && (HAS_DISPLAY || HAS_BUTTON)
namespace {
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
    action["note"] = "Repeats until stopped. Stop audio stops playback; Dismiss alarm ends the alarm session.";
}
ActionResult run_alarm_mp3(const ActionResolvedArgument* args, const char* label, uint32_t token) {
    return run_sound_operation(args, label, token, "mp3_loop", true);
}
const ActionOperationDef kAlarmMp3Operations[] = {
    {"alarm.mp3", "Loop alarm MP3", kSoundFileArguments, 2, run_alarm_mp3},
};
DEFINE_AND_REGISTER_ACTION_TYPE(kAlarmMp3ActionType, ACTION_TYPE_ALARM_MP3, describe_alarm_mp3, alarm_mp3_available,
    ACTION_EXECUTION_SYNC, true, false, kAlarmMp3Operations,
    sizeof(kAlarmMp3Operations) / sizeof(kAlarmMp3Operations[0]));
}
#endif