#include "action_catalog.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_continuation.h"
#include "action_registry.h"

namespace {

JsonObject add_action(JsonArray actions, const char* type, const char* group, const char* label) {
    JsonObject action = actions.createNestedObject();
    action["type"] = type;
    action["group"] = group;
    action["label"] = label;
    return action;
}

void add_command(JsonArray commands, const char* id, const char* label) {
    JsonObject command = commands.createNestedObject();
    command["id"] = id;
    command["label"] = label;
}

void add_field(JsonObject action, bool include_field_docs, const char* name, const char* description) {
    if (!include_field_docs) return;
    JsonArray fields = action["fields"].is<JsonArray>()
        ? action["fields"].as<JsonArray>() : action.createNestedArray("fields");
    JsonObject field = fields.createNestedObject();
    field["name"] = name;
    field["description"] = description;
}

} // namespace

// Legacy built-ins remain listed below while they are incrementally migrated.
// Registered built-ins and device-class actions are emitted from the live
// registry, so their metadata and availability share one definition.
void action_catalog_emit(JsonArray actions, bool include_field_docs) {
#if HAS_DISPLAY
    {
        JsonObject a = add_action(actions, "screen", "Navigation", "Navigate to screen");
        add_field(a, include_field_docs, "target", "screen id to navigate to");
    }
    {
        JsonObject a = add_action(actions, "cycle_pad", "Navigation", "Navigate pad sequence");
        add_field(a, include_field_docs, "direction", "next or previous (default next)");
        add_field(a, include_field_docs, "wrap", "boolean, default true");
        add_field(a, include_field_docs, "excluded_pads", "optional comma-separated 1-based pad numbers to skip");
    }
#endif
#if HAS_MQTT
    {
        JsonObject a = add_action(actions, "mqtt", "Connectivity", "Publish MQTT message");
        add_field(a, include_field_docs, "topic", "MQTT topic to publish to");
        add_field(a, include_field_docs, "payload", "MQTT payload");
    }
#endif
    {
        // HTTP-based (POST /api/services/<domain>/<service>), not MQTT — see
        // ha_service.h. Ungated to match its unconditional compilation.
        JsonObject a = add_action(actions, "ha_service", "Connectivity", "Call Home Assistant service");
        add_field(a, include_field_docs, "entity_id",
                  "required domain-qualified string with nonempty domain and object portions");
        add_field(a, include_field_docs, "service", "required bare string, never domain.service");
        add_field(a, include_field_docs, "data_json", "optional JSON object encoded as a string");
        if (include_field_docs) {
            a["example"] = "{\"type\":\"ha_service\",\"entity_id\":\"media_player.keuken\","
                           "\"service\":\"media_play_pause\",\"data_json\":\"{}\"}";
        }
    }
#if HAS_BLE_HID
    {
        JsonObject a = add_action(actions, "key", "BLE", "Send BLE keys");
        add_field(a, include_field_docs, "sequence", "key sequence DSL");
    }
    add_action(actions, "ble_pair", "BLE", "Start BLE pairing");
#endif
#if HAS_SOUND_PLAYER
    {
        JsonObject a = add_action(actions, "music", "Audio", "Music");
        JsonArray commands = a.createNestedArray("commands");
        add_command(commands, "play_pause", "Play/Pause");
        add_command(commands, "next", "Next track");
        add_command(commands, "previous", "Previous track");
        add_command(commands, "stop", "Stop");
        add_field(a, include_field_docs, "music_command", "one of play_pause, next, previous, stop");
    }
#endif
#if HAS_AUDIO
    {
        JsonObject a = add_action(actions, "sound_alert", "Audio", "Play sound alert");
        add_field(a, include_field_docs, "sound_alert_kind", "tone or mp3");
        add_field(a, include_field_docs, "sound_alert_pattern", "beep pattern DSL; tone alerts only");
        add_field(a, include_field_docs, "sound_alert_file", "sound filename; mp3 alerts only");
        add_field(a, include_field_docs, "sound_alert_volume", "0-100, optional");
    }
#endif
#if HAS_DISPLAY
    {
        JsonObject a = add_action(actions, "notify", "Display", "Show notification");
        add_field(a, include_field_docs, "notify_text", "message text, bindable");
        add_field(a, include_field_docs, "notify_duration_ms", "display duration in milliseconds, bindable");
        add_field(a, include_field_docs, "notify_text_color", "bindable color");
        add_field(a, include_field_docs, "notify_bg_color", "bindable color");
        add_field(a, include_field_docs, "notify_border_color", "bindable color");
        add_field(a, include_field_docs, "notify_opacity", "0-100");
        add_field(a, include_field_docs, "notify_font_size", "point size");
        add_field(a, include_field_docs, "notify_location", "screen placement");
    }
    {
        JsonObject a = add_action(actions, "visual_alert", "Display", "Visual alert");
        JsonArray commands = a.createNestedArray("commands");
        add_command(commands, "start", "Start alert");
        add_command(commands, "stop", "Stop alert");
        add_field(a, include_field_docs, "op", "start or stop; matches the selected command");
        add_field(a, include_field_docs, "color", "bindable color");
        add_field(a, include_field_docs, "pattern", "breathe, blink, or solid");
        add_field(a, include_field_docs, "period_ms", "cycle period in milliseconds");
        add_field(a, include_field_docs, "intensity", "0-100");
        add_field(a, include_field_docs, "duration_ms", "auto-stop after this many milliseconds, 0 for indefinite");
    }
    {
        JsonObject a = add_action(actions, "timer", "Timer", "Timer");
        JsonArray commands = a.createNestedArray("commands");
        add_command(commands, "toggle", "Toggle");
        add_command(commands, "start", "Start");
        add_command(commands, "stop", "Stop");
        add_command(commands, "pause", "Pause");
        add_command(commands, "resume", "Resume");
        add_command(commands, "reset", "Reset");
        add_command(commands, "set", "Set countdown");
        add_command(commands, "adjust", "Adjust countdown");
        add_field(a, include_field_docs, "timer_id", "1-3");
        add_field(a, include_field_docs, "timer_command", "matches the selected command");
        add_field(a, include_field_docs, "timer_mode", "up or down; required only for start/toggle");
        add_field(a, include_field_docs, "timer_value",
                  "countdown start positive whole seconds; set non-negative; adjust signed; "
                  "max start/set 4294967; bindable; no per-action expire_actions");
    }
#endif
    {
        JsonObject a = add_action(actions, "delay", "Timer", "Delay");
        add_field(a, include_field_docs, "duration_ms", "whole milliseconds, 1-55000");
        a["max_pending_actions"] = ACTION_CONTINUATION_SLOTS;
    }
    {
        // reboot/wifi_reconnect always compile; screensaver needs a display to sleep.
        JsonObject a = add_action(actions, "system", "Device", "Device command");
        JsonArray commands = a.createNestedArray("commands");
        add_command(commands, "reboot", "Restart device");
        add_command(commands, "wifi_reconnect", "Reconnect Wi-Fi");
#if HAS_DISPLAY
        add_command(commands, "screensaver", "Enable screensaver");
#endif
        add_field(a, include_field_docs, "system_command",
                  "reboot, wifi_reconnect, or screensaver; matches the selected command");
    }

    // Registered built-ins and device-class action types self-register and
    // supply their own description. A type absent from this build is not
    // catalog-visible, matching authoring validation.
    for (uint8_t i = 0; i < action_type_count(); ++i) {
        const ActionTypeDef* type = action_type_at(i);
        if (!type || !type->type_name || !action_type_is_supported(type->type_name)) continue;
        JsonObject action = actions.createNestedObject();
        action["type"] = type->type_name;
        if (type->describe) {
            type->describe(action);
            if (!include_field_docs) action.remove("fields");
        } else {
            action["group"] = "Device";
            action["label"] = type->type_name;
        }
    }
}

#endif // HAS_DISPLAY || HAS_BUTTON
