#include "action_registry.h"
#if ALARM_ENABLED && (HAS_DISPLAY || HAS_BUTTON)
namespace {
const char* validate_alarm_tone(JsonObjectConst action) {
    if (action.containsKey("sound_alert_kind")) return "Loop Tone has a fixed sound kind";
    return validate_sound_alert_payload(action, "tone_loop");
}
void parse_alarm_tone(const JsonObject& action, ButtonAction& output) {
    if (validate_alarm_tone(action)) { memset(&output, 0, sizeof(output)); return; }
    parse_sound_alert_payload(action, output, "tone_loop");
}
void serialize_alarm_tone(const ButtonAction& input, JsonObject action) {
    serialize_sound_alert(input, action);
    action.remove("sound_alert_kind");
}
void describe_alarm_tone(JsonObject& action) {
    action["group"] = "Alarm";
    action["label"] = "Loop Tone";
    describe_alarm_audio(action, false);
}
ActionResult run_alarm_tone(const ActionResolvedArgument* args, const char* label, uint32_t token) {
    return run_sound_operation(args, label, token, "tone_loop", false);
}
const ActionOperationDef kAlarmToneOperations[] = {
    {"alarm.tone", "Loop alarm tone", kSoundToneArguments, 2, run_alarm_tone},
};
DEFINE_AND_REGISTER_ACTION_TYPE(kAlarmToneActionType, ACTION_TYPE_ALARM_TONE, parse_alarm_tone, serialize_alarm_tone,
    dispatch_sound_alert, visit_sound_alert_fields, describe_alarm_tone, sound_alert_available, validate_alarm_tone,
    ACTION_EXECUTION_SYNC, true, true, false, nullptr, kAlarmToneOperations,
    sizeof(kAlarmToneOperations) / sizeof(kAlarmToneOperations[0]));
}
#endif