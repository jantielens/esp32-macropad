#include "action_registry.h"
#if ALARM_ENABLED && (HAS_DISPLAY || HAS_BUTTON)
namespace {
void describe_alarm_tone(JsonObject& action) {
    action["group"] = "Alarm";
    action["label"] = "Loop Tone";
    action["note"] = "Repeats until stopped. Stop audio stops playback; Dismiss alarm ends the alarm session.";
}
ActionResult run_alarm_tone(const ActionResolvedArgument* args, const char* label, uint32_t token) {
    return run_sound_operation(args, label, token, "tone_loop", false);
}
const ActionOperationDef kAlarmToneOperations[] = {
    {"alarm.tone", "Loop alarm tone", kSoundToneArguments, 2, run_alarm_tone},
};
DEFINE_AND_REGISTER_ACTION_TYPE(kAlarmToneActionType, ACTION_TYPE_ALARM_TONE, describe_alarm_tone, sound_alert_available,
    ACTION_EXECUTION_SYNC, true, false, kAlarmToneOperations,
    sizeof(kAlarmToneOperations) / sizeof(kAlarmToneOperations[0]));
}
#endif