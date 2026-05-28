#include "action_parse.h"

#if HAS_DISPLAY

#include "action_registry.h"
#include <string.h>

// ============================================================================
// ButtonAction JSON parse / serialize
// ============================================================================
// ButtonAction is a discriminated union — only the arm matching `type` is
// valid. parse and serialize dispatch on `type` so they touch exactly one
// arm. The on-disk JSON shape is intentionally *flat* (keys at the top
// level) so existing saved configs continue to round-trip without
// migration; the mapping from flat key → union arm field is centralized
// in this file.

void action_parse(const JsonObject& a, ButtonAction& act) {
    memset(&act, 0, sizeof(ButtonAction));
    strlcpy(act.type, a["type"] | "", sizeof(act.type));
    if (!act.type[0]) return;

    if (strcmp(act.type, ACTION_TYPE_SCREEN) == 0) {
        strlcpy(act.payload.screen.screen_id, a["target"] | "", sizeof(act.payload.screen.screen_id));
    } else if (strcmp(act.type, ACTION_TYPE_MQTT) == 0) {
        strlcpy(act.payload.mqtt.mqtt_topic,   a["topic"]   | "", sizeof(act.payload.mqtt.mqtt_topic));
        strlcpy(act.payload.mqtt.mqtt_payload, a["payload"] | "", sizeof(act.payload.mqtt.mqtt_payload));
    } else if (strcmp(act.type, ACTION_TYPE_KEY) == 0) {
        strlcpy(act.payload.key.key_sequence, a["sequence"] | "", sizeof(act.payload.key.key_sequence));
    } else if (strcmp(act.type, ACTION_TYPE_BEEP) == 0) {
        strlcpy(act.payload.beep.beep_pattern, a["beep_pattern"] | "", sizeof(act.payload.beep.beep_pattern));
        act.payload.beep.beep_volume = (uint8_t)(a["beep_volume"] | 0);
    } else if (strcmp(act.type, ACTION_TYPE_VOLUME) == 0) {
        strlcpy(act.payload.volume.volume_mode,  a["volume_mode"]  | "", sizeof(act.payload.volume.volume_mode));
        strlcpy(act.payload.volume.volume_value, a["volume_value"] | "", sizeof(act.payload.volume.volume_value));
    } else if (strcmp(act.type, ACTION_TYPE_BRIGHTNESS) == 0) {
        strlcpy(act.payload.brightness.brightness_mode,  a["brightness_mode"]  | "", sizeof(act.payload.brightness.brightness_mode));
        strlcpy(act.payload.brightness.brightness_value, a["brightness_value"] | "", sizeof(act.payload.brightness.brightness_value));
    } else if (strcmp(act.type, ACTION_TYPE_TIMER) == 0) {
        act.payload.timer.timer_id = (uint8_t)(a["timer_id"] | 0);
        strlcpy(act.payload.timer.timer_command, a["timer_command"] | "", sizeof(act.payload.timer.timer_command));
        strlcpy(act.payload.timer.timer_value,   a["timer_value"]   | "", sizeof(act.payload.timer.timer_value));
    } else if (strcmp(act.type, ACTION_TYPE_SOUND) == 0) {
        strlcpy(act.payload.sound.sound_file, a["sound_file"] | "", sizeof(act.payload.sound.sound_file));
        act.payload.sound.sound_volume = (uint8_t)(a["sound_volume"] | 0);
    } else if (strcmp(act.type, ACTION_TYPE_NOTIFY) == 0) {
        strlcpy(act.payload.notify.notify_text,         a["notify_text"]         | "", sizeof(act.payload.notify.notify_text));
        strlcpy(act.payload.notify.notify_duration_ms,  a["notify_duration_ms"]  | "", sizeof(act.payload.notify.notify_duration_ms));
        strlcpy(act.payload.notify.notify_text_color,   a["notify_text_color"]   | "", sizeof(act.payload.notify.notify_text_color));
        strlcpy(act.payload.notify.notify_bg_color,     a["notify_bg_color"]     | "", sizeof(act.payload.notify.notify_bg_color));
        strlcpy(act.payload.notify.notify_border_color, a["notify_border_color"] | "", sizeof(act.payload.notify.notify_border_color));
        act.payload.notify.notify_opacity   = (uint8_t)(a["notify_opacity"]   | 0);
        act.payload.notify.notify_font_size = (uint8_t)(a["notify_font_size"] | 0);
        strlcpy(act.payload.notify.notify_location,     a["notify_location"]     | "", sizeof(act.payload.notify.notify_location));
    } else if (strcmp(act.type, ACTION_TYPE_SYSTEM) == 0) {
        strlcpy(act.payload.system.system_command, a["system_command"] | "", sizeof(act.payload.system.system_command));
    } else {
        // Device-class action types (e.g. shutter) self-register via
        // action_type_register(); fall through to the registry.
        const ActionTypeDef* t = action_type_find(act.type);
        if (t && t->parse) t->parse(a, act);
        // back, ble_pair, none: type tag only — no payload to parse.
    }
}

void action_to_json(const ButtonAction& act, JsonObject obj) {
    if (!act.type[0]) return;  // empty action → empty object
    obj["type"] = act.type;

    if (strcmp(act.type, ACTION_TYPE_SCREEN) == 0) {
        if (act.payload.screen.screen_id[0]) obj["target"] = act.payload.screen.screen_id;
    } else if (strcmp(act.type, ACTION_TYPE_MQTT) == 0) {
        if (act.payload.mqtt.mqtt_topic[0])   obj["topic"]   = act.payload.mqtt.mqtt_topic;
        if (act.payload.mqtt.mqtt_payload[0]) obj["payload"] = act.payload.mqtt.mqtt_payload;
    } else if (strcmp(act.type, ACTION_TYPE_KEY) == 0) {
        if (act.payload.key.key_sequence[0]) obj["sequence"] = act.payload.key.key_sequence;
    } else if (strcmp(act.type, ACTION_TYPE_BEEP) == 0) {
        if (act.payload.beep.beep_pattern[0]) obj["beep_pattern"] = act.payload.beep.beep_pattern;
        if (act.payload.beep.beep_volume > 0) obj["beep_volume"]  = act.payload.beep.beep_volume;
    } else if (strcmp(act.type, ACTION_TYPE_VOLUME) == 0) {
        if (act.payload.volume.volume_mode[0])  obj["volume_mode"]  = act.payload.volume.volume_mode;
        if (act.payload.volume.volume_value[0]) obj["volume_value"] = act.payload.volume.volume_value;
    } else if (strcmp(act.type, ACTION_TYPE_BRIGHTNESS) == 0) {
        if (act.payload.brightness.brightness_mode[0])  obj["brightness_mode"]  = act.payload.brightness.brightness_mode;
        if (act.payload.brightness.brightness_value[0]) obj["brightness_value"] = act.payload.brightness.brightness_value;
    } else if (strcmp(act.type, ACTION_TYPE_TIMER) == 0) {
        if (act.payload.timer.timer_id > 0)     obj["timer_id"]      = act.payload.timer.timer_id;
        if (act.payload.timer.timer_command[0]) obj["timer_command"] = act.payload.timer.timer_command;
        if (act.payload.timer.timer_value[0])   obj["timer_value"]   = act.payload.timer.timer_value;
    } else if (strcmp(act.type, ACTION_TYPE_SOUND) == 0) {
        if (act.payload.sound.sound_file[0])    obj["sound_file"]   = act.payload.sound.sound_file;
        if (act.payload.sound.sound_volume > 0) obj["sound_volume"] = act.payload.sound.sound_volume;
    } else if (strcmp(act.type, ACTION_TYPE_NOTIFY) == 0) {
        if (act.payload.notify.notify_text[0])         obj["notify_text"]         = act.payload.notify.notify_text;
        if (act.payload.notify.notify_duration_ms[0])  obj["notify_duration_ms"]  = act.payload.notify.notify_duration_ms;
        if (act.payload.notify.notify_text_color[0])   obj["notify_text_color"]   = act.payload.notify.notify_text_color;
        if (act.payload.notify.notify_bg_color[0])     obj["notify_bg_color"]     = act.payload.notify.notify_bg_color;
        if (act.payload.notify.notify_border_color[0]) obj["notify_border_color"] = act.payload.notify.notify_border_color;
        if (act.payload.notify.notify_opacity > 0)     obj["notify_opacity"]      = act.payload.notify.notify_opacity;
        if (act.payload.notify.notify_font_size > 0)   obj["notify_font_size"]    = act.payload.notify.notify_font_size;
        if (act.payload.notify.notify_location[0])     obj["notify_location"]     = act.payload.notify.notify_location;
    } else if (strcmp(act.type, ACTION_TYPE_SYSTEM) == 0) {
        if (act.payload.system.system_command[0]) obj["system_command"] = act.payload.system.system_command;
    } else {
        const ActionTypeDef* t = action_type_find(act.type);
        if (t && t->serialize) t->serialize(act, obj);
        // back, ble_pair, none: type tag only — no payload to serialize.
    }
}

#endif // HAS_DISPLAY
