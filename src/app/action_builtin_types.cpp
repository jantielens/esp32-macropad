#include "action_registry.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_dispatch.h"
#include "action_parse_builtin.h"
#include "action_continuation.h"
#include "music_command.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace {

void add_editor_field(JsonObject& action, const char* name, const char* label,
                      const char* type, bool bindable = false,
                      bool command_options = false) {
    JsonArray fields = action["editor_fields"].is<JsonArray>()
        ? action["editor_fields"].as<JsonArray>()
        : action.createNestedArray("editor_fields");
    JsonObject field = fields.createNestedObject();
    field["name"] = name;
    field["label"] = label;
    field["type"] = type;
    if (bindable) field["bindable"] = true;
    if (command_options) field["command_options"] = true;
}

void add_field_doc(JsonObject& action, const char* name, const char* description) {
    JsonArray fields = action["fields"].is<JsonArray>()
        ? action["fields"].as<JsonArray>()
        : action.createNestedArray("fields");
    JsonObject field = fields.createNestedObject();
    field["name"] = name;
    field["description"] = description;
}

const char* validate_command(JsonObjectConst action, const char* field,
                             const char* action_name) {
    if (!action.containsKey(field)) return nullptr;
    if (!action[field].is<const char*>()) return "action command must be a string";
    const char* command = action[field].as<const char*>();
    if (strcmp(command, "set") == 0 || strcmp(command, "adjust") == 0) return nullptr;
    return action_name;
}

const char* validate_volume(JsonObjectConst action) {
    return validate_command(action, "volume_mode", "volume_mode must be set or adjust");
}

const char* validate_brightness(JsonObjectConst action) {
    return validate_command(action, "brightness_mode", "brightness_mode must be set or adjust");
}

void describe_back(JsonObject& action) {
    action["group"] = "Navigation";
    action["label"] = "Navigate back";
}

void describe_volume(JsonObject& action) {
    action["group"] = "Audio";
    action["label"] = "Volume";
    JsonArray commands = action.createNestedArray("commands");
    JsonObject set = commands.createNestedObject();
    set["id"] = "set";
    set["label"] = "Set volume";
    JsonObject adjust = commands.createNestedObject();
    adjust["id"] = "adjust";
    adjust["label"] = "Adjust volume";
    add_field_doc(action, "volume_mode", "set or adjust; matches the selected command");
    add_field_doc(action, "volume_value", "percentage for set; signed delta for adjust");
    add_editor_field(action, "volume_mode", "Command", "select", false, true);
    add_editor_field(action, "volume_value", "Value (%)", "text", true);
}

void describe_brightness(JsonObject& action) {
    action["group"] = "Display";
    action["label"] = "Brightness";
    JsonArray commands = action.createNestedArray("commands");
    JsonObject set = commands.createNestedObject();
    set["id"] = "set";
    set["label"] = "Set brightness";
    JsonObject adjust = commands.createNestedObject();
    adjust["id"] = "adjust";
    adjust["label"] = "Adjust brightness";
    add_field_doc(action, "brightness_mode", "set or adjust; matches the selected command");
    add_field_doc(action, "brightness_value", "percentage for set; signed delta for adjust");
    add_editor_field(action, "brightness_mode", "Command", "select", false, true);
    add_editor_field(action, "brightness_value", "Value (%)", "text", true);
}

void describe_system(JsonObject& action) {
    action["group"] = "Device";
    action["label"] = "Device command";
    JsonArray commands = action.createNestedArray("commands");
    JsonObject reboot = commands.createNestedObject();
    reboot["id"] = "reboot";
    reboot["label"] = "Restart device";
    JsonObject reconnect = commands.createNestedObject();
    reconnect["id"] = "wifi_reconnect";
    reconnect["label"] = "Reconnect Wi-Fi";
#if HAS_DISPLAY
    JsonObject screensaver = commands.createNestedObject();
    screensaver["id"] = "screensaver";
    screensaver["label"] = "Enable screensaver";
#endif
    add_field_doc(action, "system_command", "matches the selected command");
    add_editor_field(action, "system_command", "Command", "select", false, true);
}

bool music_available() {
#if HAS_SOUND_PLAYER
    return true;
#else
    return false;
#endif
}

const char* validate_music(JsonObjectConst action) {
    if (!action.containsKey("music_command")) return "music missing music_command";
    if (!action["music_command"].is<const char*>()) return "music_command must be a string";
    MusicCommand command;
    return music_command_parse(action["music_command"].as<const char*>(), &command)
        ? nullptr : "music_command must be play_pause, next, previous, or stop";
}

void describe_music(JsonObject& action) {
    action["group"] = "Audio";
    action["label"] = "Music";
    JsonArray commands = action.createNestedArray("commands");
    const MusicCommand values[] = {
        MUSIC_COMMAND_PLAY_PAUSE, MUSIC_COMMAND_NEXT,
        MUSIC_COMMAND_PREVIOUS, MUSIC_COMMAND_STOP,
    };
    const char* labels[] = { "Play/Pause", "Next track", "Previous track", "Stop" };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        JsonObject command = commands.createNestedObject();
        command["id"] = music_command_name(values[i]);
        command["label"] = labels[i];
    }
    add_field_doc(action, "music_command", "matches the selected command");
    add_editor_field(action, "music_command", "Command", "select", false, true);
}

bool display_available() {
#if HAS_DISPLAY
    return true;
#else
    return false;
#endif
}

bool ble_available() {
#if HAS_BLE_HID
    return true;
#else
    return false;
#endif
}

char* screen_value_field(ButtonAction& act, size_t* out_size) {
    *out_size = sizeof(act.payload.screen.screen_id);
    return act.payload.screen.screen_id;
}

char* key_value_field(ButtonAction& act, size_t* out_size) {
    *out_size = sizeof(act.payload.key.key_sequence);
    return act.payload.key.key_sequence;
}

const char* validate_delay(JsonObjectConst action) {
    if (!action.containsKey("duration_ms") || !action["duration_ms"].is<uint32_t>()) {
        return "delay duration_ms must be a whole number";
    }
    return action_delay_duration_is_valid(action["duration_ms"].as<uint32_t>())
        ? nullptr : "delay duration_ms must be 1-55000";
}

void describe_screen(JsonObject& action) {
    action["group"] = "Navigation";
    action["label"] = "Navigate to screen";
    add_field_doc(action, "target", "screen id to navigate to");
}

void describe_key(JsonObject& action) {
    action["group"] = "BLE";
    action["label"] = "Send BLE keys";
    add_field_doc(action, "sequence", "key sequence DSL");
}

void describe_ble_pair(JsonObject& action) {
    action["group"] = "BLE";
    action["label"] = "Start BLE pairing";
}

void describe_delay(JsonObject& action) {
    action["group"] = "Timer";
    action["label"] = "Delay";
    action["max_pending_actions"] = ACTION_CONTINUATION_SLOTS;
    add_field_doc(action, "duration_ms", "whole milliseconds, 1-55000");
}

bool mqtt_available() {
#if HAS_MQTT
    return true;
#else
    return false;
#endif
}

bool audio_available() {
#if HAS_AUDIO
    return true;
#else
    return false;
#endif
}

const char* validate_cycle_pad(JsonObjectConst action) {
    if (action.containsKey("direction")) {
        if (!action["direction"].is<const char*>()) return "cycle_pad direction must be a string";
        const char* direction = action["direction"].as<const char*>();
        if (strcmp(direction, "next") != 0 && strcmp(direction, "previous") != 0) {
            return "cycle_pad direction must be 'next' or 'previous'";
        }
    }
    if (action.containsKey("wrap") && !action["wrap"].is<bool>()) return "cycle_pad wrap must be boolean";
    if (action.containsKey("excluded_pads") && !action["excluded_pads"].is<const char*>()) {
        return "cycle_pad excluded_pads must be a string";
    }
    return nullptr;
}

const char* validate_ha_service(JsonObjectConst action) {
    if (!action.containsKey("entity_id")) return "ha_service missing entity_id";
    if (!action["entity_id"].is<const char*>()) return "ha_service entity_id must be a string";
    const char* entity_id = action["entity_id"].as<const char*>();
    if (!entity_id[0]) return "ha_service entity_id must not be empty";
    if (strlen(entity_id) >= sizeof(((HaServicePayload*)nullptr)->entity_id)) return "ha_service entity_id too long";
    const char* separator = strchr(entity_id, '.');
    if (!separator || separator == entity_id || !separator[1] || strchr(separator + 1, '.')) {
        return "ha_service entity_id must have nonempty domain and object portions";
    }
    for (const char* cursor = entity_id; *cursor; ++cursor) {
        if (isspace((unsigned char)*cursor)) return "ha_service entity_id must not contain whitespace";
    }
    if (!action.containsKey("service")) return "ha_service missing service";
    if (!action["service"].is<const char*>()) return "ha_service service must be a string";
    const char* service = action["service"].as<const char*>();
    if (!service[0]) return "ha_service service must not be empty";
    const char* service_separator = strrchr(service, '.');
    if (service_separator) {
        static char error[96];
        snprintf(error, sizeof(error), "service must be bare; use '%s'", service_separator + 1);
        return error;
    }
    if (strlen(service) >= sizeof(((HaServicePayload*)nullptr)->service)) return "ha_service service too long";
    for (const char* cursor = service; *cursor; ++cursor) {
        unsigned char character = (unsigned char)*cursor;
        if (!islower(character) && !isdigit(character) && character != '_') {
            return "ha_service service must contain only lowercase letters, digits, and '_'";
        }
    }
    if (!action.containsKey("data_json")) return nullptr;
    if (!action["data_json"].is<const char*>()) return "ha_service data_json must be a string";
    const char* data_json = action["data_json"].as<const char*>();
    if (!data_json[0]) return nullptr;
    if (strlen(data_json) >= sizeof(((HaServicePayload*)nullptr)->data_json)) return "ha_service data_json too long";
    JsonDocument data;
    if (deserializeJson(data, data_json)) return "ha_service data_json must contain valid JSON";
    return data.is<JsonObjectConst>() ? nullptr : "ha_service data_json root must be an object";
}

void describe_cycle_pad(JsonObject& action) {
    action["group"] = "Navigation";
    action["label"] = "Navigate pad sequence";
    add_field_doc(action, "direction", "next or previous (default next)");
    add_field_doc(action, "wrap", "boolean, default true");
    add_field_doc(action, "excluded_pads", "optional comma-separated 1-based pad numbers to skip");
}

void describe_mqtt(JsonObject& action) {
    action["group"] = "Connectivity";
    action["label"] = "Publish MQTT message";
    add_field_doc(action, "topic", "MQTT topic to publish to");
    add_field_doc(action, "payload", "MQTT payload");
}

void describe_ha_service(JsonObject& action) {
    action["group"] = "Connectivity";
    action["label"] = "Call Home Assistant service";
    add_field_doc(action, "entity_id", "required domain-qualified string with nonempty domain and object portions");
    add_field_doc(action, "service", "required bare string, never domain.service");
    add_field_doc(action, "data_json", "optional JSON object encoded as a string");
}

void describe_sound_alert(JsonObject& action) {
    action["group"] = "Audio";
    action["label"] = "Play sound alert";
    add_field_doc(action, "sound_alert_kind", "tone or mp3");
    add_field_doc(action, "sound_alert_pattern", "beep pattern DSL; tone alerts only");
    add_field_doc(action, "sound_alert_file", "sound filename; mp3 alerts only");
    add_field_doc(action, "sound_alert_volume", "0-100, optional");
}

void describe_notify(JsonObject& action) {
    action["group"] = "Display";
    action["label"] = "Show notification";
    add_field_doc(action, "notify_text", "message text, bindable");
    add_field_doc(action, "notify_duration_ms", "display duration in milliseconds, bindable");
    add_field_doc(action, "notify_text_color", "bindable color");
    add_field_doc(action, "notify_bg_color", "bindable color");
    add_field_doc(action, "notify_border_color", "bindable color");
    add_field_doc(action, "notify_opacity", "0-100");
    add_field_doc(action, "notify_font_size", "point size");
    add_field_doc(action, "notify_location", "screen placement");
}

void describe_visual_alert(JsonObject& action) {
    action["group"] = "Display";
    action["label"] = "Visual alert";
    JsonArray commands = action.createNestedArray("commands");
    JsonObject start = commands.createNestedObject();
    start["id"] = "start";
    start["label"] = "Start alert";
    JsonObject stop = commands.createNestedObject();
    stop["id"] = "stop";
    stop["label"] = "Stop alert";
    add_field_doc(action, "op", "start or stop; matches the selected command");
    add_field_doc(action, "color", "bindable color");
    add_field_doc(action, "pattern", "breathe, blink, or solid");
    add_field_doc(action, "period_ms", "cycle period in milliseconds");
    add_field_doc(action, "intensity", "0-100");
    add_field_doc(action, "duration_ms", "auto-stop after this many milliseconds, 0 for indefinite");
}

void describe_timer(JsonObject& action) {
    action["group"] = "Timer";
    action["label"] = "Timer";
    JsonArray commands = action.createNestedArray("commands");
    const char* const command_ids[] = { "toggle", "start", "stop", "pause", "resume", "reset", "set", "adjust" };
    const char* const command_labels[] = { "Toggle", "Start", "Stop", "Pause", "Resume", "Reset", "Set countdown", "Adjust countdown" };
    for (size_t i = 0; i < sizeof(command_ids) / sizeof(command_ids[0]); ++i) {
        JsonObject command = commands.createNestedObject();
        command["id"] = command_ids[i];
        command["label"] = command_labels[i];
    }
    add_field_doc(action, "timer_id", "1-3");
    add_field_doc(action, "timer_command", "matches the selected command");
    add_field_doc(action, "timer_mode", "up or down; required only for start/toggle");
    add_field_doc(action, "timer_value", "countdown start positive whole seconds; set non-negative; adjust signed; max start/set 4294967; bindable; no per-action expire_actions");
}

bool visit_field(ActionBindableFieldVisitor visitor, void* context, char* field,
                 size_t size, bool reject_overflow = false) {
    return !field[0] || visitor(field, size, reject_overflow, context);
}

bool visit_mqtt_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) {
    return visit_field(visitor, context, act.payload.mqtt.mqtt_topic, sizeof(act.payload.mqtt.mqtt_topic))
        && visit_field(visitor, context, act.payload.mqtt.mqtt_payload, sizeof(act.payload.mqtt.mqtt_payload));
}

bool visit_volume_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) {
    return visit_field(visitor, context, act.payload.volume.volume_value,
                       sizeof(act.payload.volume.volume_value));
}

bool visit_brightness_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) {
    return visit_field(visitor, context, act.payload.brightness.brightness_value,
                       sizeof(act.payload.brightness.brightness_value));
}

bool visit_sound_alert_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) {
    return strcmp(act.payload.sound_alert.sound_alert_kind, "tone") != 0
        || visit_field(visitor, context, act.payload.sound_alert.sound_alert_pattern,
                       sizeof(act.payload.sound_alert.sound_alert_pattern));
}

bool visit_timer_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) {
    return visit_field(visitor, context, act.payload.timer.timer_value,
                       sizeof(act.payload.timer.timer_value), true);
}

bool visit_notify_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) {
    return visit_field(visitor, context, act.payload.notify.notify_text, sizeof(act.payload.notify.notify_text))
        && visit_field(visitor, context, act.payload.notify.notify_duration_ms, sizeof(act.payload.notify.notify_duration_ms))
        && visit_field(visitor, context, act.payload.notify.notify_text_color, sizeof(act.payload.notify.notify_text_color))
        && visit_field(visitor, context, act.payload.notify.notify_bg_color, sizeof(act.payload.notify.notify_bg_color))
        && visit_field(visitor, context, act.payload.notify.notify_border_color, sizeof(act.payload.notify.notify_border_color));
}

bool visit_visual_alert_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) {
    return visit_field(visitor, context, act.payload.visual_alert.va_color,
                       sizeof(act.payload.visual_alert.va_color));
}

DEFINE_AND_REGISTER_ACTION_TYPE(kBackActionType,
    ACTION_TYPE_BACK, nullptr, nullptr, action_dispatch_back, nullptr, describe_back
);

#if HAS_AUDIO
DEFINE_AND_REGISTER_ACTION_TYPE(kVolumeActionType,
    ACTION_TYPE_VOLUME, action_parse_volume, action_serialize_volume,
    action_dispatch_volume, nullptr, describe_volume, nullptr, validate_volume, visit_volume_fields
);
#endif

#if HAS_DISPLAY
DEFINE_AND_REGISTER_ACTION_TYPE(kBrightnessActionType,
    ACTION_TYPE_BRIGHTNESS, action_parse_brightness, action_serialize_brightness,
    action_dispatch_brightness, nullptr, describe_brightness, nullptr, validate_brightness, visit_brightness_fields
);
#endif

DEFINE_AND_REGISTER_ACTION_TYPE(kSystemActionType,
    ACTION_TYPE_SYSTEM, action_parse_system, action_serialize_system,
    action_dispatch_system, nullptr, describe_system
);

DEFINE_AND_REGISTER_ACTION_TYPE(kMusicActionType,
    ACTION_TYPE_MUSIC, action_parse_music, action_serialize_music,
    action_dispatch_music, nullptr, describe_music, music_available, validate_music
);

DEFINE_AND_REGISTER_ACTION_TYPE(kScreenActionType,
    ACTION_TYPE_SCREEN, action_parse_screen, action_serialize_screen,
    action_dispatch_screen, screen_value_field, describe_screen, display_available
);

DEFINE_AND_REGISTER_ACTION_TYPE(kKeyActionType,
    ACTION_TYPE_KEY, action_parse_key, action_serialize_key,
    action_dispatch_key, key_value_field, describe_key, ble_available
);

DEFINE_AND_REGISTER_ACTION_TYPE(kBlePairActionType,
    ACTION_TYPE_BLE_PAIR, nullptr, nullptr,
    action_dispatch_ble_pair, nullptr, describe_ble_pair, ble_available
);

DEFINE_AND_REGISTER_ACTION_TYPE(kDelayActionType,
    ACTION_TYPE_DELAY, action_parse_delay, action_serialize_delay,
    action_dispatch_delay, nullptr, describe_delay, nullptr, validate_delay
);

DEFINE_AND_REGISTER_ACTION_TYPE(kCyclePadActionType,
    ACTION_TYPE_CYCLE_PAD, action_parse_cycle_pad, action_serialize_cycle_pad,
    action_dispatch_cycle_pad, nullptr, describe_cycle_pad, display_available, validate_cycle_pad
);

DEFINE_AND_REGISTER_ACTION_TYPE(kMqttActionType,
    ACTION_TYPE_MQTT, action_parse_mqtt, action_serialize_mqtt,
    action_dispatch_mqtt, nullptr, describe_mqtt, mqtt_available, nullptr, visit_mqtt_fields
);

DEFINE_AND_REGISTER_ACTION_TYPE(kHaServiceActionType,
    ACTION_TYPE_HA_SERVICE, action_parse_ha_service, action_serialize_ha_service,
    action_dispatch_ha_service, nullptr, describe_ha_service, nullptr, validate_ha_service
);

DEFINE_AND_REGISTER_ACTION_TYPE(kSoundAlertActionType,
    ACTION_TYPE_SOUND_ALERT, action_parse_sound_alert, action_serialize_sound_alert,
    action_dispatch_sound_alert, nullptr, describe_sound_alert, audio_available, nullptr, visit_sound_alert_fields
);

DEFINE_AND_REGISTER_ACTION_TYPE(kNotifyActionType,
    ACTION_TYPE_NOTIFY, action_parse_notify, action_serialize_notify,
    action_dispatch_notify, nullptr, describe_notify, display_available, nullptr, visit_notify_fields
);

DEFINE_AND_REGISTER_ACTION_TYPE(kVisualAlertActionType,
    ACTION_TYPE_VISUAL_ALERT, action_parse_visual_alert, action_serialize_visual_alert,
    action_dispatch_visual_alert, nullptr, describe_visual_alert, display_available, nullptr, visit_visual_alert_fields
);

DEFINE_AND_REGISTER_ACTION_TYPE(kTimerActionType,
    ACTION_TYPE_TIMER, action_parse_timer, action_serialize_timer,
    action_dispatch_timer, nullptr, describe_timer, display_available, nullptr, visit_timer_fields
);


} // namespace

#endif // HAS_DISPLAY || HAS_BUTTON