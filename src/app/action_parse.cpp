#include "action_parse_builtin.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_registry.h"
#include "pad_cycle.h"
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

void action_parse_volume(const JsonObject& a, ButtonAction& act) {
    strlcpy(act.payload.volume.volume_mode, a["volume_mode"] | "",
            sizeof(act.payload.volume.volume_mode));
    strlcpy(act.payload.volume.volume_value, a["volume_value"] | "",
            sizeof(act.payload.volume.volume_value));
}

void action_serialize_volume(const ButtonAction& act, JsonObject obj) {
    if (act.payload.volume.volume_mode[0]) obj["volume_mode"] = act.payload.volume.volume_mode;
    if (act.payload.volume.volume_value[0]) obj["volume_value"] = act.payload.volume.volume_value;
}

void action_parse_brightness(const JsonObject& a, ButtonAction& act) {
    strlcpy(act.payload.brightness.brightness_mode, a["brightness_mode"] | "",
            sizeof(act.payload.brightness.brightness_mode));
    strlcpy(act.payload.brightness.brightness_value, a["brightness_value"] | "",
            sizeof(act.payload.brightness.brightness_value));
}

void action_serialize_brightness(const ButtonAction& act, JsonObject obj) {
    if (act.payload.brightness.brightness_mode[0]) obj["brightness_mode"] = act.payload.brightness.brightness_mode;
    if (act.payload.brightness.brightness_value[0]) obj["brightness_value"] = act.payload.brightness.brightness_value;
}

void action_parse_system(const JsonObject& a, ButtonAction& act) {
    strlcpy(act.payload.system.system_command, a["system_command"] | "",
            sizeof(act.payload.system.system_command));
}

void action_serialize_system(const ButtonAction& act, JsonObject obj) {
    if (act.payload.system.system_command[0]) obj["system_command"] = act.payload.system.system_command;
}

void action_parse_music(const JsonObject& a, ButtonAction& act) {
    const char* command = a["music_command"] | "";
    if (strcmp(command, "play_pause") != 0 && strcmp(command, "next") != 0 &&
        strcmp(command, "previous") != 0 && strcmp(command, "stop") != 0) {
        memset(&act, 0, sizeof(act));
        return;
    }
    strlcpy(act.payload.music.music_command, command, sizeof(act.payload.music.music_command));
}

void action_serialize_music(const ButtonAction& act, JsonObject obj) {
    if (act.payload.music.music_command[0]) obj["music_command"] = act.payload.music.music_command;
}

void action_parse_screen(const JsonObject& a, ButtonAction& act) {
    strlcpy(act.payload.screen.screen_id, a["target"] | "", sizeof(act.payload.screen.screen_id));
}

void action_serialize_screen(const ButtonAction& act, JsonObject obj) {
    if (act.payload.screen.screen_id[0]) obj["target"] = act.payload.screen.screen_id;
}

void action_parse_key(const JsonObject& a, ButtonAction& act) {
    strlcpy(act.payload.key.key_sequence, a["sequence"] | "", sizeof(act.payload.key.key_sequence));
}

void action_serialize_key(const ButtonAction& act, JsonObject obj) {
    if (act.payload.key.key_sequence[0]) obj["sequence"] = act.payload.key.key_sequence;
}

void action_parse_delay(const JsonObject& a, ButtonAction& act) {
    if (!a["duration_ms"].is<uint32_t>()) {
        memset(&act, 0, sizeof(act));
        return;
    }
    act.payload.delay.duration_ms = a["duration_ms"].as<uint32_t>();
    if (!action_delay_duration_is_valid(act.payload.delay.duration_ms)) {
        memset(&act, 0, sizeof(act));
    }
}

void action_serialize_delay(const ButtonAction& act, JsonObject obj) {
    obj["duration_ms"] = act.payload.delay.duration_ms;
}

void action_parse_mqtt(const JsonObject& a, ButtonAction& act) {
    strlcpy(act.payload.mqtt.mqtt_topic, a["topic"] | "", sizeof(act.payload.mqtt.mqtt_topic));
    strlcpy(act.payload.mqtt.mqtt_payload, a["payload"] | "", sizeof(act.payload.mqtt.mqtt_payload));
}

void action_serialize_mqtt(const ButtonAction& act, JsonObject obj) {
    if (act.payload.mqtt.mqtt_topic[0]) obj["topic"] = act.payload.mqtt.mqtt_topic;
    if (act.payload.mqtt.mqtt_payload[0]) obj["payload"] = act.payload.mqtt.mqtt_payload;
}

void action_parse_timer(const JsonObject& a, ButtonAction& act) {
    const char* command = a["timer_command"] | "";
    const char* mode = a["timer_mode"] | "";
    const char* value = a["timer_value"] | "";
    if (strlen(command) >= sizeof(act.payload.timer.timer_command)
            || strlen(mode) >= sizeof(act.payload.timer.timer_mode)
            || strlen(value) >= sizeof(act.payload.timer.timer_value)) {
        memset(&act, 0, sizeof(act));
        return;
    }
    act.payload.timer.timer_id = (uint8_t)(a["timer_id"] | 0);
    strlcpy(act.payload.timer.timer_command, command, sizeof(act.payload.timer.timer_command));
    strlcpy(act.payload.timer.timer_mode, mode, sizeof(act.payload.timer.timer_mode));
    strlcpy(act.payload.timer.timer_value, value, sizeof(act.payload.timer.timer_value));
}

void action_serialize_timer(const ButtonAction& act, JsonObject obj) {
    if (act.payload.timer.timer_id > 0) obj["timer_id"] = act.payload.timer.timer_id;
    if (act.payload.timer.timer_command[0]) obj["timer_command"] = act.payload.timer.timer_command;
    if (act.payload.timer.timer_mode[0]) obj["timer_mode"] = act.payload.timer.timer_mode;
    if (act.payload.timer.timer_value[0]) obj["timer_value"] = act.payload.timer.timer_value;
}

void action_parse_sound_alert(const JsonObject& a, ButtonAction& act) {
    const char* kind = a["sound_alert_kind"] | "";
    const char* pattern = a["sound_alert_pattern"] | "";
    const char* file = a["sound_alert_file"] | "";
    const uint8_t volume = (uint8_t)(a["sound_alert_volume"] | 0);
    if ((strcmp(kind, "tone") != 0 && strcmp(kind, "mp3") != 0) || volume > 100 ||
        (strcmp(kind, "tone") == 0 && file[0]) ||
        (strcmp(kind, "mp3") == 0 && (pattern[0] || !file[0]))) {
        memset(&act, 0, sizeof(act));
        return;
    }
    strlcpy(act.payload.sound_alert.sound_alert_kind, kind, sizeof(act.payload.sound_alert.sound_alert_kind));
    strlcpy(act.payload.sound_alert.sound_alert_pattern, pattern, sizeof(act.payload.sound_alert.sound_alert_pattern));
    strlcpy(act.payload.sound_alert.sound_alert_file, file, sizeof(act.payload.sound_alert.sound_alert_file));
    act.payload.sound_alert.sound_alert_volume = volume;
}

void action_serialize_sound_alert(const ButtonAction& act, JsonObject obj) {
    obj["sound_alert_kind"] = act.payload.sound_alert.sound_alert_kind;
    if (strcmp(act.payload.sound_alert.sound_alert_kind, "tone") == 0 && act.payload.sound_alert.sound_alert_pattern[0]) obj["sound_alert_pattern"] = act.payload.sound_alert.sound_alert_pattern;
    if (strcmp(act.payload.sound_alert.sound_alert_kind, "mp3") == 0 && act.payload.sound_alert.sound_alert_file[0]) obj["sound_alert_file"] = act.payload.sound_alert.sound_alert_file;
    if (act.payload.sound_alert.sound_alert_volume > 0) obj["sound_alert_volume"] = act.payload.sound_alert.sound_alert_volume;
}

void action_parse_notify(const JsonObject& a, ButtonAction& act) {
    strlcpy(act.payload.notify.notify_text, a["notify_text"] | "", sizeof(act.payload.notify.notify_text));
    strlcpy(act.payload.notify.notify_duration_ms, a["notify_duration_ms"] | "", sizeof(act.payload.notify.notify_duration_ms));
    strlcpy(act.payload.notify.notify_text_color, a["notify_text_color"] | "", sizeof(act.payload.notify.notify_text_color));
    strlcpy(act.payload.notify.notify_bg_color, a["notify_bg_color"] | "", sizeof(act.payload.notify.notify_bg_color));
    strlcpy(act.payload.notify.notify_border_color, a["notify_border_color"] | "", sizeof(act.payload.notify.notify_border_color));
    act.payload.notify.notify_opacity = (uint8_t)(a["notify_opacity"] | 0);
    act.payload.notify.notify_font_size = (uint8_t)(a["notify_font_size"] | 0);
    strlcpy(act.payload.notify.notify_location, a["notify_location"] | "", sizeof(act.payload.notify.notify_location));
}

void action_serialize_notify(const ButtonAction& act, JsonObject obj) {
    if (act.payload.notify.notify_text[0]) obj["notify_text"] = act.payload.notify.notify_text;
    if (act.payload.notify.notify_duration_ms[0]) obj["notify_duration_ms"] = act.payload.notify.notify_duration_ms;
    if (act.payload.notify.notify_text_color[0]) obj["notify_text_color"] = act.payload.notify.notify_text_color;
    if (act.payload.notify.notify_bg_color[0]) obj["notify_bg_color"] = act.payload.notify.notify_bg_color;
    if (act.payload.notify.notify_border_color[0]) obj["notify_border_color"] = act.payload.notify.notify_border_color;
    if (act.payload.notify.notify_opacity > 0) obj["notify_opacity"] = act.payload.notify.notify_opacity;
    if (act.payload.notify.notify_font_size > 0) obj["notify_font_size"] = act.payload.notify.notify_font_size;
    if (act.payload.notify.notify_location[0]) obj["notify_location"] = act.payload.notify.notify_location;
}

void action_parse_ha_service(const JsonObject& a, ButtonAction& act) {
    strlcpy(act.payload.ha_service.entity_id, a["entity_id"] | "", sizeof(act.payload.ha_service.entity_id));
    strlcpy(act.payload.ha_service.service, a["service"] | "", sizeof(act.payload.ha_service.service));
    strlcpy(act.payload.ha_service.data_json, a["data_json"] | "", sizeof(act.payload.ha_service.data_json));
}

void action_serialize_ha_service(const ButtonAction& act, JsonObject obj) {
    if (act.payload.ha_service.entity_id[0]) obj["entity_id"] = act.payload.ha_service.entity_id;
    if (act.payload.ha_service.service[0]) obj["service"] = act.payload.ha_service.service;
    if (act.payload.ha_service.data_json[0]) obj["data_json"] = act.payload.ha_service.data_json;
}

void action_parse_visual_alert(const JsonObject& a, ButtonAction& act) {
    strlcpy(act.payload.visual_alert.va_op, a["op"] | "", sizeof(act.payload.visual_alert.va_op));
    strlcpy(act.payload.visual_alert.va_color, a["color"] | "", sizeof(act.payload.visual_alert.va_color));
    strlcpy(act.payload.visual_alert.va_pattern, a["pattern"] | "", sizeof(act.payload.visual_alert.va_pattern));
    act.payload.visual_alert.va_period_ms = (uint16_t)(a["period_ms"] | 0);
    act.payload.visual_alert.va_intensity = (uint16_t)(a["intensity"] | 0);
    act.payload.visual_alert.va_duration_ms = (uint32_t)(a["duration_ms"] | 0);
}

void action_serialize_visual_alert(const ButtonAction& act, JsonObject obj) {
    if (act.payload.visual_alert.va_op[0]) obj["op"] = act.payload.visual_alert.va_op;
    if (act.payload.visual_alert.va_color[0]) obj["color"] = act.payload.visual_alert.va_color;
    if (act.payload.visual_alert.va_pattern[0]) obj["pattern"] = act.payload.visual_alert.va_pattern;
    if (act.payload.visual_alert.va_period_ms > 0) obj["period_ms"] = act.payload.visual_alert.va_period_ms;
    if (act.payload.visual_alert.va_intensity > 0) obj["intensity"] = act.payload.visual_alert.va_intensity;
    if (act.payload.visual_alert.va_duration_ms > 0) obj["duration_ms"] = act.payload.visual_alert.va_duration_ms;
}

void action_parse_cycle_pad(const JsonObject& a, ButtonAction& act) {
    if ((a.containsKey("direction") && !a["direction"].is<const char*>()) ||
        (a.containsKey("wrap") && !a["wrap"].is<bool>()) ||
        (a.containsKey("excluded_pads") && !a["excluded_pads"].is<const char*>())) {
        memset(&act, 0, sizeof(act));
        return;
    }
    const char* direction = a["direction"] | "next";
    if (strcmp(direction, "next") != 0 && strcmp(direction, "previous") != 0) {
        memset(&act, 0, sizeof(act));
        return;
    }
    act.payload.cycle_pad.direction = strcmp(direction, "previous") == 0 ? -1 : 1;
    act.payload.cycle_pad.wrap = a["wrap"] | true;
    act.payload.cycle_pad.excluded_mask = pad_cycle_parse_exclusions(a["excluded_pads"] | "");
}

void action_serialize_cycle_pad(const ButtonAction& act, JsonObject obj) {
    obj["direction"] = act.payload.cycle_pad.direction < 0 ? "previous" : "next";
    obj["wrap"] = act.payload.cycle_pad.wrap;
    char exclusions[MAX_PADS * 3 + 1];
    pad_cycle_format_exclusions(act.payload.cycle_pad.excluded_mask, exclusions, sizeof(exclusions));
    if (exclusions[0]) obj["excluded_pads"] = exclusions;
}

void action_parse(const JsonObject& a, ButtonAction& act) {
    memset(&act, 0, sizeof(ButtonAction));
    strlcpy(act.type, a["type"] | "", sizeof(act.type));
    if (!act.type[0]) return;

    const ActionTypeDef* type = action_type_find(act.type);
    if (type && type->parse) {
        type->parse(a, act);
    } else if (strcmp(act.type, "beep") == 0 || strcmp(act.type, "sound") == 0) {
        memset(&act, 0, sizeof(ButtonAction));
    }
}

void action_to_json(const ButtonAction& act, JsonObject obj) {
    if (!act.type[0]) return;  // empty action → empty object
    obj["type"] = act.type;

    const ActionTypeDef* type = action_type_find(act.type);
    if (type && type->serialize) type->serialize(act, obj);
}

#endif // HAS_DISPLAY
