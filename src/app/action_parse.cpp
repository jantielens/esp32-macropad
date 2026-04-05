#include "action_parse.h"

#if HAS_DISPLAY

#include <string.h>

void action_parse(const JsonObject& a, ButtonAction& act) {
    memset(&act, 0, sizeof(ButtonAction));
    strlcpy(act.type, a["type"] | "", CONFIG_ACTION_TYPE_MAX_LEN);
    strlcpy(act.screen_id, a["target"] | "", CONFIG_SCREEN_ID_MAX_LEN);
    strlcpy(act.mqtt_topic, a["topic"] | "", CONFIG_MQTT_TOPIC_MAX_LEN);
    strlcpy(act.mqtt_payload, a["payload"] | "", CONFIG_MQTT_PAYLOAD_MAX_LEN);
    strlcpy(act.key_sequence, a["sequence"] | "", CONFIG_KEY_SEQ_MAX_LEN);
    strlcpy(act.beep_pattern, a["beep_pattern"] | "", CONFIG_BEEP_PATTERN_MAX_LEN);
    act.beep_volume = (uint8_t)(a["beep_volume"] | 0);
    strlcpy(act.volume_mode, a["volume_mode"] | "", CONFIG_VOLUME_MODE_MAX_LEN);
    act.volume_value = (uint8_t)(a["volume_value"] | 0);
    // Timer: "timer_command" from web → reuse mqtt_payload for storage
    if (strcmp(act.type, ACTION_TYPE_TIMER) == 0 && a.containsKey("timer_command")) {
        strlcpy(act.mqtt_payload, a["timer_command"] | "", CONFIG_MQTT_PAYLOAD_MAX_LEN);
    }
    strlcpy(act.sound_file, a["sound_file"] | "", sizeof(act.sound_file));
    act.sound_volume = (uint8_t)(a["sound_volume"] | 0);
}

void action_to_json(const ButtonAction& act, JsonObject obj) {
    if (!act.type[0]) return;  // empty action → empty object
    obj["type"] = act.type;
    if (act.screen_id[0])    obj["target"]   = act.screen_id;
    if (act.mqtt_topic[0])   obj["topic"]    = act.mqtt_topic;
    if (act.mqtt_payload[0]) {
        if (strcmp(act.type, ACTION_TYPE_TIMER) == 0) {
            obj["timer_command"] = act.mqtt_payload;
        } else {
            obj["payload"] = act.mqtt_payload;
        }
    }
    if (act.key_sequence[0]) obj["sequence"] = act.key_sequence;
    if (act.beep_pattern[0])  obj["beep_pattern"]  = act.beep_pattern;
    if (act.beep_volume > 0)  obj["beep_volume"]   = act.beep_volume;
    if (act.volume_mode[0])   obj["volume_mode"]   = act.volume_mode;
    if (act.volume_value > 0) obj["volume_value"]  = act.volume_value;
    if (act.sound_file[0])    obj["sound_file"]    = act.sound_file;
    if (act.sound_volume > 0) obj["sound_volume"]  = act.sound_volume;
}

#endif // HAS_DISPLAY
