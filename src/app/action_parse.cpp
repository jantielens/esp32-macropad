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
    strlcpy(act.brightness_mode, a["brightness_mode"] | "", CONFIG_VOLUME_MODE_MAX_LEN);
    act.brightness_value = (uint8_t)(a["brightness_value"] | 0);
    // Timer: "timer_command" from web → reuse mqtt_payload for storage
    if (strcmp(act.type, ACTION_TYPE_TIMER) == 0 && a.containsKey("timer_command")) {
        strlcpy(act.mqtt_payload, a["timer_command"] | "", CONFIG_MQTT_PAYLOAD_MAX_LEN);
    }
    strlcpy(act.sound_file, a["sound_file"] | "", sizeof(act.sound_file));
    act.sound_volume = (uint8_t)(a["sound_volume"] | 0);
    // Notify fields
    strlcpy(act.notify_text, a["notify_text"] | "", sizeof(act.notify_text));
    strlcpy(act.notify_duration_ms, a["notify_duration_ms"] | "", CONFIG_BINDABLE_SHORT_LEN);
    strlcpy(act.notify_text_color, a["notify_text_color"] | "", CONFIG_BINDABLE_SHORT_LEN);
    strlcpy(act.notify_bg_color, a["notify_bg_color"] | "", CONFIG_BINDABLE_SHORT_LEN);
    strlcpy(act.notify_border_color, a["notify_border_color"] | "", CONFIG_BINDABLE_SHORT_LEN);
    act.notify_opacity = (uint8_t)(a["notify_opacity"] | 0);
    act.notify_font_size = (uint8_t)(a["notify_font_size"] | 0);
    strlcpy(act.notify_location, a["notify_location"] | "", sizeof(act.notify_location));
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
    if (act.brightness_mode[0])   obj["brightness_mode"]   = act.brightness_mode;
    if (act.brightness_value > 0) obj["brightness_value"]  = act.brightness_value;
    if (act.sound_file[0])    obj["sound_file"]    = act.sound_file;
    if (act.sound_volume > 0) obj["sound_volume"]  = act.sound_volume;
    // Notify fields
    if (act.notify_text[0])           obj["notify_text"]         = act.notify_text;
    if (act.notify_duration_ms[0])    obj["notify_duration_ms"]  = act.notify_duration_ms;
    if (act.notify_text_color[0])     obj["notify_text_color"]   = act.notify_text_color;
    if (act.notify_bg_color[0])       obj["notify_bg_color"]     = act.notify_bg_color;
    if (act.notify_border_color[0])   obj["notify_border_color"] = act.notify_border_color;
    if (act.notify_opacity > 0)       obj["notify_opacity"]      = act.notify_opacity;
    if (act.notify_font_size > 0)     obj["notify_font_size"]    = act.notify_font_size;
    if (act.notify_location[0])       obj["notify_location"]     = act.notify_location;
}

#endif // HAS_DISPLAY
