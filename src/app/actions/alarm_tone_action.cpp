#include "action_registry.h"
#if ALARM_ENABLED && (HAS_DISPLAY || HAS_BUTTON)
namespace {
const char* validate_alarm_tone(JsonObjectConst action) {
    if (action.containsKey("sound_alert_kind")) return "Alarm tone has a fixed sound kind";
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
    action["label"] = "Alarm tone";
    describe_alarm_audio(action, false);
}
DEFINE_AND_REGISTER_ACTION_TYPE(kAlarmToneActionType, ACTION_TYPE_ALARM_TONE, parse_alarm_tone, serialize_alarm_tone,
    dispatch_sound_alert, nullptr, describe_alarm_tone, sound_alert_available, validate_alarm_tone,
    visit_sound_alert_fields, ACTION_EXECUTION_SYNC);
}
#endif