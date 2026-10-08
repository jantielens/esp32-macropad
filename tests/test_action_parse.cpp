// ============================================================================
// Unit tests for action_parse() and action_to_json() round-trip
// ============================================================================
// Verifies that all ButtonAction fields survive a parse→serialize→parse cycle
// and that edge cases (empty actions, missing fields, timer_command mapping)
// are handled correctly.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ArduinoJson.h>
#include "action_result.h"
#include "pad_config.h"
#include "action_list.h"
#include "action_parse.h"
#include "action_registry.h"
#include "action_catalog.h"
#include <string>

ActionResult action_dispatch(const ButtonAction&, const char*, uint32_t) {
    return ACTION_COMPLETE;
}

extern "C" unsigned long millis() { return 0; }

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) static void test_##name()
#define RUN(name)  do { \
    printf("  %-50s ", #name); \
    test_##name(); \
    printf("PASS\n"); \
    g_pass++; \
} while(0)

#define ASSERT_STR(field, expected) do { \
    if (strcmp((field), (expected)) != 0) { \
        printf("FAIL\n    %s: expected \"%s\", got \"%s\"\n", #field, (expected), (field)); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_EQ(field, expected) do { \
    if ((field) != (expected)) { \
        printf("FAIL\n    %s: expected %d, got %d\n", #field, (int)(expected), (int)(field)); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    assertion failed: %s\n", #cond); \
        g_fail++; return; \
    } \
} while(0)

// Helper: parse JSON string into a ButtonAction
static ButtonAction parse_from_string(const char* json_str) {
    StaticJsonDocument<1024> doc;
    deserializeJson(doc, json_str);
    ButtonAction act;
    action_parse(doc.as<JsonObject>(), act);
    return act;
}

// Helper: round-trip parse→serialize→parse and return the final action
static ButtonAction round_trip(const char* json_str) {
    ButtonAction first = parse_from_string(json_str);
    // Serialize
    StaticJsonDocument<1024> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(first, obj);
    // Re-parse
    ButtonAction second;
    action_parse(obj, second);
    return second;
}

// ============================================================================
// Empty / minimal actions
// ============================================================================

TEST(empty_json) {
    ButtonAction act = parse_from_string("{}");
    ASSERT_STR(act.type, "");
    // With type=="" no arm is active; spot-check zero-init of a couple arms.
    ASSERT_EQ(act.payload.sound_alert.sound_alert_volume, 0);
}

TEST(empty_to_json_produces_empty_object) {
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(act, obj);
    ASSERT_EQ(obj.size(), 0);  // empty action → no keys
}

TEST(action_list_filters_literal_none_for_pad_callers) {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, "[{\"type\":\"none\"},{\"type\":\"back\"},{},null]");
    ButtonAction actions[MAX_BUTTON_ACTIONS];

    uint8_t count = action_list_parse(doc.as<JsonVariant>(), actions,
                                      MAX_BUTTON_ACTIONS, true);
    ASSERT_EQ(count, 1);
    ASSERT_STR(actions[0].type, "back");
    ASSERT_STR(actions[1].type, "");
}

TEST(action_list_retains_literal_none_for_existing_callers) {
    StaticJsonDocument<256> doc;
    deserializeJson(doc, "[{\"type\":\"none\"}]");
    ButtonAction actions[MAX_BUTTON_ACTIONS];

    uint8_t count = action_list_parse(doc.as<JsonVariant>(), actions,
                                      MAX_BUTTON_ACTIONS);
    ASSERT_EQ(count, 1);
    ASSERT_STR(actions[0].type, "none");
}

TEST(type_only) {
    ButtonAction act = parse_from_string("{\"type\":\"back\"}");
    ASSERT_STR(act.type, "back");
    // back carries no payload
}

TEST(gamepad_round_trip_and_validation) {
    ButtonAction act = round_trip("{\"type\":\"gamepad\",\"control\":\"button\",\"button\":16,\"operation\":\"down\"}");
    ASSERT_STR(act.type, "gamepad");
    ASSERT_EQ(act.payload.gamepad.index, 15);
    ASSERT_EQ(act.payload.gamepad.operation, 1);
    act = round_trip("{\"type\":\"gamepad\",\"control\":\"hat\",\"direction\":\"right\",\"operation\":\"up\"}");
    ASSERT_EQ(act.payload.gamepad.control, 1);
    ASSERT_EQ(act.payload.gamepad.index, 3);
    act = round_trip("{\"type\":\"gamepad\",\"control\":\"trigger\",\"trigger\":\"left\"}");
    ASSERT_EQ(act.payload.gamepad.control, 2);
    ASSERT_EQ(act.payload.gamepad.operation, 0);
    act = parse_from_string("{\"type\":\"gamepad\",\"button\":17}");
    ASSERT_STR(act.type, "");
    act = parse_from_string("{\"type\":\"gamepad\",\"control\":\"hat\",\"direction\":\"up\",\"button\":1}");
    ASSERT_STR(act.type, "");
    act = parse_from_string("{\"type\":\"gamepad\",\"control\":\"trigger\",\"trigger\":\"invalid\"}");
    ASSERT_STR(act.type, "");
}

// ============================================================================
// Screen action
// ============================================================================

TEST(screen_action_parse) {
    ButtonAction act = parse_from_string("{\"type\":\"screen\",\"target\":\"pad_3\"}");
    ASSERT_STR(act.type, "screen");
    ASSERT_STR(act.payload.screen.screen_id, "pad_3");
}

TEST(screen_action_round_trip) {
    ButtonAction act = round_trip("{\"type\":\"screen\",\"target\":\"pad_0\"}");
    ASSERT_STR(act.type, "screen");
    ASSERT_STR(act.payload.screen.screen_id, "pad_0");
}

// ============================================================================
// MQTT action
// ============================================================================

TEST(mqtt_action_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"mqtt\",\"topic\":\"home/light\",\"payload\":\"ON\"}");
    ASSERT_STR(act.type, "mqtt");
    ASSERT_STR(act.payload.mqtt.mqtt_topic, "home/light");
    ASSERT_STR(act.payload.mqtt.mqtt_payload, "ON");
}

TEST(mqtt_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"mqtt\",\"topic\":\"home/light\",\"payload\":\"toggle\"}");
    ASSERT_STR(act.type, "mqtt");
    ASSERT_STR(act.payload.mqtt.mqtt_topic, "home/light");
    ASSERT_STR(act.payload.mqtt.mqtt_payload, "toggle");
}

// ============================================================================
// Key action
// ============================================================================

TEST(key_action_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"key\",\"sequence\":\"Ctrl+C\"}");
    ASSERT_STR(act.type, "key");
    ASSERT_STR(act.payload.key.key_sequence, "Ctrl+C");
}

TEST(key_action_round_trip) {
    ButtonAction act = round_trip("{\"type\":\"key\",\"sequence\":\"Alt+Tab\"}");
    ASSERT_STR(act.type, "key");
    ASSERT_STR(act.payload.key.key_sequence, "Alt+Tab");
}

TEST(mouse_button_action_round_trip) {
    for (const char* button : {"left", "right", "middle"}) {
        char json[100];
        snprintf(json, sizeof(json), "{\"type\":\"mouse_button\",\"button\":\"%s\"}", button);
        ButtonAction act = round_trip(json);
        ASSERT_STR(act.type, "mouse_button");
        ASSERT_STR(act.payload.mouse_button.button, button);
    }
    ButtonAction act = round_trip("{\"type\":\"mouse_button\"}");
    ASSERT_STR(act.payload.mouse_button.button, "left");
}

TEST(mouse_button_validation_and_catalog) {
    const ActionTypeDef* type = action_type_find(ACTION_TYPE_MOUSE_BUTTON);
    ASSERT_TRUE(type != nullptr);
    ASSERT_EQ(type->available(), HAS_USB_HID);
    ASSERT_TRUE(type->value_field == nullptr);
    for (const char* value : {"\"invalid\"", "\"\"", "3", "null", "true"}) {
        char json[100];
        snprintf(json, sizeof(json), "{\"type\":\"mouse_button\",\"button\":%s}", value);
        JsonDocument doc;
        deserializeJson(doc, json);
        ASSERT_TRUE(action_type_validate(type, doc.as<JsonObjectConst>()) != nullptr);
        ButtonAction act = parse_from_string(json);
        ASSERT_STR(act.type, "");
    }
    JsonDocument doc;
    JsonObject catalog = doc.to<JsonObject>();
    type->describe(catalog);
    ASSERT_EQ(catalog["commands"].size(), 3);
    ASSERT_STR(catalog["commands"][0]["id"].as<const char*>(), "left");
    ASSERT_STR(catalog["commands"][1]["id"].as<const char*>(), "right");
    ASSERT_STR(catalog["commands"][2]["id"].as<const char*>(), "middle");
    ASSERT_STR(catalog["editor_fields"][0]["name"].as<const char*>(), "button");
    ASSERT_TRUE(catalog["editor_fields"][0]["command_options"].as<bool>());
}

// ============================================================================
// Music action
// ============================================================================

TEST(music_action_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"music\",\"music_command\":\"play_pause\"}");
    ASSERT_STR(act.type, "music");
    ASSERT_STR(act.payload.music.music_command, "play_pause");
}

TEST(music_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"music\",\"music_command\":\"previous\"}");
    ASSERT_STR(act.type, "music");
    ASSERT_STR(act.payload.music.music_command, "previous");
}

// Sound Alert action
// ============================================================================

TEST(sound_alert_tone_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"sound_alert\",\"sound_alert_kind\":\"tone\",\"sound_alert_pattern\":\"1000:200\",\"sound_alert_volume\":80}");
    ASSERT_STR(act.type, "sound_alert");
    ASSERT_STR(act.payload.sound_alert.sound_alert_kind, "tone");
    ASSERT_STR(act.payload.sound_alert.sound_alert_pattern, "1000:200");
    ASSERT_EQ(act.payload.sound_alert.sound_alert_volume, 80);
}

TEST(sound_alert_mp3_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"sound_alert\",\"sound_alert_kind\":\"mp3\",\"sound_alert_file\":\"startup\",\"sound_alert_volume\":100}");
    ASSERT_STR(act.type, "sound_alert");
    ASSERT_STR(act.payload.sound_alert.sound_alert_kind, "mp3");
    ASSERT_STR(act.payload.sound_alert.sound_alert_file, "startup");
    ASSERT_EQ(act.payload.sound_alert.sound_alert_volume, 100);
}

TEST(legacy_alert_aliases_are_rejected) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"beep\",\"beep_pattern\":\"1000:100\"}");
    ASSERT_STR(act.type, "");
    act = parse_from_string("{\"type\":\"sound\",\"sound_file\":\"chime\"}");
    ASSERT_STR(act.type, "");
}

TEST(alarm_audio_round_trip) {
#if ALARM_ENABLED
    ButtonAction control = round_trip("{\"type\":\"alarm\",\"alarm_command\":\"adjust_minutes\",\"alarm_value\":\"{step}\"}");
    ASSERT_STR(control.type, "alarm");
    ASSERT_EQ(control.payload.alarm.alarm_id, 1);
    action_type_substitute_step(action_type_find("alarm"), control, -60);
    ASSERT_STR(control.payload.alarm.alarm_value, "-60");
    control = round_trip("{\"type\":\"alarm\",\"alarm_command\":\"weekday_toggle\",\"alarm_day\":6}");
    ASSERT_EQ(control.payload.alarm.alarm_day, 6);
    ASSERT_STR(parse_from_string("{\"type\":\"alarm\",\"alarm_command\":\"bad\"}").type, "");
    ASSERT_STR(parse_from_string("{\"type\":\"alarm\",\"alarm_command\":\"set_time\",\"alarm_value\":\"1440\"}").type, "");
    ASSERT_STR(parse_from_string("{\"type\":\"alarm\",\"alarm_command\":\"adjust_minutes\",\"alarm_value\":\"1.5\"}").type, "");
    ASSERT_STR(parse_from_string("{\"type\":\"alarm\",\"alarm_command\":\"toggle\",\"alarm_id\":0}").type, "");
    ASSERT_STR(parse_from_string("{\"type\":\"alarm\",\"alarm_command\":\"weekday_toggle\"}").type, "");
    ButtonAction tone = round_trip("{\"type\":\"alarm_tone\",\"sound_alert_pattern\":\"1000:200 800\",\"sound_alert_volume\":55}");
    ASSERT_STR(tone.type, "alarm_tone");
    ASSERT_STR(tone.payload.sound_alert.sound_alert_kind, "tone_loop");
    ASSERT_STR(tone.payload.sound_alert.sound_alert_pattern, "1000:200 800");
    ASSERT_EQ(tone.payload.sound_alert.sound_alert_volume, 55);
    ButtonAction mp3 = round_trip("{\"type\":\"alarm_mp3\",\"sound_alert_file\":\"wake-up\",\"sound_alert_volume\":60}");
    ASSERT_STR(mp3.type, "alarm_mp3");
    ASSERT_STR(mp3.payload.sound_alert.sound_alert_kind, "mp3_loop");
    ASSERT_STR(mp3.payload.sound_alert.sound_alert_file, "wake-up");
    ASSERT_EQ(mp3.payload.sound_alert.sound_alert_volume, 60);
    ASSERT_STR(parse_from_string("{\"type\":\"alarm_mp3\"}").type, "");
    ASSERT_STR(parse_from_string("{\"type\":\"alarm_tone\",\"sound_alert_file\":\"wake-up\"}").type, "");
    ASSERT_STR(parse_from_string("{\"type\":\"alarm_mp3\",\"sound_alert_file\":\"wake-up\",\"sound_alert_volume\":101}").type, "");
    ASSERT_STR(parse_from_string("{\"type\":\"alarm_tone\",\"sound_alert_kind\":\"stop\"}").type, "");
    StaticJsonDocument<1024> description;
    JsonObject metadata = description.to<JsonObject>();
    action_type_find("alarm_mp3")->describe(metadata);
    ASSERT_STR(metadata["group"] | "", "Alarm");
    ASSERT_STR(metadata["label"] | "", "Alarm MP3");
    ASSERT_EQ(action_type_find("alarm_tone")->execution, ACTION_EXECUTION_SYNC);
    ASSERT_STR(parse_from_string("{\"type\":\"sound_alert\",\"sound_alert_kind\":\"tone_loop\",\"sound_alert_pattern\":\"1000:200 800\"}").type, "");
    ASSERT_STR(parse_from_string("{\"type\":\"sound_alert\",\"sound_alert_kind\":\"mp3_loop\",\"sound_alert_file\":\"wake-up\"}").type, "");
#else
    for (const char* type : {"alarm", "alarm_tone", "alarm_mp3"}) {
        ASSERT_TRUE(action_type_find(type) == nullptr);
        ASSERT_TRUE(!action_type_is_supported(type));
    }
#endif
}

// ============================================================================
// Volume action
// ============================================================================

TEST(volume_set_action_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"volume\",\"volume_mode\":\"set\",\"volume_value\":\"60\"}");
    ASSERT_STR(act.type, "volume");
    ASSERT_STR(act.payload.volume.volume_mode, "set");
    ASSERT_STR(act.payload.volume.volume_value, "60");
}

TEST(volume_adjust_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"volume\",\"volume_mode\":\"adjust\",\"volume_value\":\"-10\"}");
    ASSERT_STR(act.type, "volume");
    ASSERT_STR(act.payload.volume.volume_mode, "adjust");
    ASSERT_STR(act.payload.volume.volume_value, "-10");
}

TEST(volume_adjust_step_placeholder) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"volume\",\"volume_mode\":\"adjust\",\"volume_value\":\"{step}\"}");
    ASSERT_STR(act.payload.volume.volume_value, "{step}");
}

// ============================================================================
// Brightness action
// ============================================================================

TEST(brightness_set_action_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"brightness\",\"brightness_mode\":\"set\",\"brightness_value\":\"75\"}");
    ASSERT_STR(act.type, "brightness");
    ASSERT_STR(act.payload.brightness.brightness_mode, "set");
    ASSERT_STR(act.payload.brightness.brightness_value, "75");
}

TEST(brightness_adjust_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"brightness\",\"brightness_mode\":\"adjust\",\"brightness_value\":\"-5\"}");
    ASSERT_STR(act.type, "brightness");
    ASSERT_STR(act.payload.brightness.brightness_mode, "adjust");
    ASSERT_STR(act.payload.brightness.brightness_value, "-5");
}

TEST(brightness_empty_value_not_serialized) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"brightness\",\"brightness_mode\":\"set\"}");
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(act, obj);
    ASSERT_TRUE(!obj.containsKey("brightness_value"));
    ASSERT_TRUE(obj.containsKey("brightness_mode"));
}

#if HAS_LVGL_EPAPER
TEST(display_refresh_action_round_trip) {
    ButtonAction act = round_trip("{\"type\":\"display_refresh\",\"mode\":\"full\"}");
    ASSERT_STR(act.type, "display_refresh");
    ASSERT_STR(act.payload.display_refresh.mode, "full");
}
#endif

// ============================================================================
// Timer action
// ============================================================================

TEST(timer_action_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"timer\",\"timer_id\":1,\"timer_command\":\"toggle\",\"timer_mode\":\"up\"}");
    ASSERT_STR(act.type, "timer");
    ASSERT_EQ(act.payload.timer.timer_id, 1);
    ASSERT_STR(act.payload.timer.timer_command, "toggle");
    ASSERT_STR(act.payload.timer.timer_mode, "up");
}

TEST(timer_countdown_start_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"timer\",\"timer_id\":3,\"timer_command\":\"start\",\"timer_mode\":\"down\",\"timer_value\":\"300\"}");
    ASSERT_STR(act.type, "timer");
    ASSERT_EQ(act.payload.timer.timer_id, 3);
    ASSERT_STR(act.payload.timer.timer_command, "start");
    ASSERT_STR(act.payload.timer.timer_mode, "down");
    ASSERT_STR(act.payload.timer.timer_value, "300");
}

TEST(timer_oversized_wire_fields_rejected_before_copy) {
    ButtonAction value = parse_from_string(
        "{\"type\":\"timer\",\"timer_id\":1,\"timer_command\":\"start\","
        "\"timer_mode\":\"down\",\"timer_value\":\"1234567890123456\"}");
    ASSERT_TRUE(value.type[0] == '\0');

    ButtonAction mode = parse_from_string(
        "{\"type\":\"timer\",\"timer_id\":1,\"timer_command\":\"start\","
        "\"timer_mode\":\"downx\",\"timer_value\":\"1\"}");
    ASSERT_TRUE(mode.type[0] == '\0');
}

TEST(timer_adjust_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"timer\",\"timer_id\":2,\"timer_command\":\"adjust\",\"timer_value\":\"30\"}");
    ASSERT_STR(act.type, "timer");
    ASSERT_EQ(act.payload.timer.timer_id, 2);
    ASSERT_STR(act.payload.timer.timer_command, "adjust");
    ASSERT_STR(act.payload.timer.timer_value, "30");
}

TEST(timer_set_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"timer\",\"timer_id\":1,\"timer_command\":\"set\",\"timer_value\":\"300\"}");
    ASSERT_STR(act.type, "timer");
    ASSERT_EQ(act.payload.timer.timer_id, 1);
    ASSERT_STR(act.payload.timer.timer_command, "set");
    ASSERT_STR(act.payload.timer.timer_value, "300");
}

TEST(timer_fields_serialized_properly) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"timer\",\"timer_id\":3,\"timer_command\":\"start\",\"timer_mode\":\"up\"}");
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(act, obj);
    ASSERT_TRUE(obj.containsKey("timer_id"));
    ASSERT_TRUE(obj.containsKey("timer_command"));
    ASSERT_TRUE(obj.containsKey("timer_mode"));
    ASSERT_EQ(obj["timer_id"].as<int>(), 3);
    ASSERT_STR(obj["timer_command"].as<const char*>(), "start");
    ASSERT_STR(obj["timer_mode"].as<const char*>(), "up");
}

TEST(mqtt_payload_serialized_as_payload_not_timer_command) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"mqtt\",\"topic\":\"test\",\"payload\":\"ON\"}");
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(act, obj);
    ASSERT_TRUE(obj.containsKey("payload"));
    ASSERT_TRUE(!obj.containsKey("timer_command"));
}

// ============================================================================
// BLE pair action
// ============================================================================

TEST(ble_pair_action_parse) {
    ButtonAction act = parse_from_string("{\"type\":\"ble_pair\"}");
    ASSERT_STR(act.type, "ble_pair");
}

TEST(ble_pair_action_round_trip) {
    ButtonAction act = round_trip("{\"type\":\"ble_pair\"}");
    ASSERT_STR(act.type, "ble_pair");
}

// ============================================================================
// Back action
// ============================================================================

TEST(back_action_round_trip) {
    ButtonAction act = round_trip("{\"type\":\"back\"}");
    ASSERT_STR(act.type, "back");
}

// ============================================================================
// Missing fields default to zero/empty
// ============================================================================

TEST(missing_optional_fields) {
    ButtonAction act = parse_from_string("{\"type\":\"sound_alert\",\"sound_alert_kind\":\"tone\"}");
    ASSERT_STR(act.payload.sound_alert.sound_alert_pattern, "");
    ASSERT_EQ(act.payload.sound_alert.sound_alert_volume, 0);
}

// ============================================================================
// Compact serialization: zero/empty fields are omitted
// ============================================================================

TEST(compact_serialization) {
    ButtonAction act = parse_from_string("{\"type\":\"back\"}");
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(act, obj);
    ASSERT_EQ(obj.size(), 1);  // only "type"
    ASSERT_STR(obj["type"].as<const char*>(), "back");
}

TEST(per_type_round_trips_cover_all_arms) {
    // ButtonAction is a discriminated union — only one arm is valid at a time,
    // so the previous "all fields at once" test no longer makes sense. This
    // test instead round-trips one action of each type and asserts the active
    // arm survives parse→serialize→parse.
    {
        ButtonAction a = round_trip("{\"type\":\"screen\",\"target\":\"pad_1\"}");
        ASSERT_STR(a.payload.screen.screen_id, "pad_1");
    }
    {
        ButtonAction a = round_trip("{\"type\":\"mqtt\",\"topic\":\"t\",\"payload\":\"p\"}");
        ASSERT_STR(a.payload.mqtt.mqtt_topic, "t");
        ASSERT_STR(a.payload.mqtt.mqtt_payload, "p");
    }
    {
        ButtonAction a = round_trip("{\"type\":\"key\",\"sequence\":\"Ctrl+A\"}");
        ASSERT_STR(a.payload.key.key_sequence, "Ctrl+A");
    }
    {
        ButtonAction a = round_trip("{\"type\":\"music\",\"music_command\":\"next\"}");
        ASSERT_STR(a.payload.music.music_command, "next");
    }
    {
        ButtonAction a = round_trip("{\"type\":\"volume\",\"volume_mode\":\"set\",\"volume_value\":\"55\"}");
        ASSERT_STR(a.payload.volume.volume_mode, "set");
        ASSERT_STR(a.payload.volume.volume_value, "55");
    }
    {
        ButtonAction a = round_trip("{\"type\":\"brightness\",\"brightness_mode\":\"adjust\",\"brightness_value\":\"-15\"}");
        ASSERT_STR(a.payload.brightness.brightness_mode, "adjust");
        ASSERT_STR(a.payload.brightness.brightness_value, "-15");
    }
    {
        ButtonAction a = round_trip("{\"type\":\"timer\",\"timer_id\":2,\"timer_command\":\"adjust\",\"timer_value\":\"30\"}");
        ASSERT_EQ(a.payload.timer.timer_id, 2);
        ASSERT_STR(a.payload.timer.timer_command, "adjust");
        ASSERT_STR(a.payload.timer.timer_value, "30");
    }
    {
        ButtonAction a = round_trip("{\"type\":\"sound_alert\",\"sound_alert_kind\":\"mp3\",\"sound_alert_file\":\"alert\",\"sound_alert_volume\":90}");
        ASSERT_STR(a.payload.sound_alert.sound_alert_file, "alert");
        ASSERT_EQ(a.payload.sound_alert.sound_alert_volume, 90);
    }
    {
        ButtonAction a = round_trip(
            "{\"type\":\"notify\",\"notify_text\":\"hello\",\"notify_duration_ms\":\"5000\","
            "\"notify_text_color\":\"#ff0000\",\"notify_bg_color\":\"#00ff00\","
            "\"notify_border_color\":\"#0000ff\",\"notify_opacity\":90,"
            "\"notify_font_size\":24,\"notify_location\":\"top\"}");
        ASSERT_STR(a.payload.notify.notify_text, "hello");
        ASSERT_STR(a.payload.notify.notify_duration_ms, "5000");
        ASSERT_STR(a.payload.notify.notify_text_color, "#ff0000");
        ASSERT_STR(a.payload.notify.notify_bg_color, "#00ff00");
        ASSERT_STR(a.payload.notify.notify_border_color, "#0000ff");
        ASSERT_EQ(a.payload.notify.notify_opacity, 90);
        ASSERT_EQ(a.payload.notify.notify_font_size, 24);
        ASSERT_STR(a.payload.notify.notify_location, "top");
    }
    {
        ButtonAction a = round_trip("{\"type\":\"system\",\"system_command\":\"reboot\"}");
        ASSERT_STR(a.payload.system.system_command, "reboot");
    }
}

// ============================================================================
// Notify action
// ============================================================================

TEST(notify_action_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"notify\",\"notify_text\":\"Power high!\","
        "\"notify_duration_ms\":\"5000\",\"notify_text_color\":\"#ffa0a0\","
        "\"notify_bg_color\":\"#2e1a1a\",\"notify_border_color\":\"#5a2a2a\","
        "\"notify_opacity\":90,\"notify_font_size\":18,\"notify_location\":\"top\"}");
    ASSERT_STR(act.type, "notify");
    ASSERT_STR(act.payload.notify.notify_text, "Power high!");
    ASSERT_STR(act.payload.notify.notify_duration_ms, "5000");
    ASSERT_STR(act.payload.notify.notify_text_color, "#ffa0a0");
    ASSERT_STR(act.payload.notify.notify_bg_color, "#2e1a1a");
    ASSERT_STR(act.payload.notify.notify_border_color, "#5a2a2a");
    ASSERT_EQ(act.payload.notify.notify_opacity, 90);
    ASSERT_EQ(act.payload.notify.notify_font_size, 18);
    ASSERT_STR(act.payload.notify.notify_location, "top");
}

TEST(notify_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"notify\",\"notify_text\":\"Test msg\","
        "\"notify_duration_ms\":\"0\",\"notify_bg_color\":\"#333333\","
        "\"notify_location\":\"center\"}");
    ASSERT_STR(act.type, "notify");
    ASSERT_STR(act.payload.notify.notify_text, "Test msg");
    ASSERT_STR(act.payload.notify.notify_duration_ms, "0");
    ASSERT_STR(act.payload.notify.notify_bg_color, "#333333");
    ASSERT_STR(act.payload.notify.notify_location, "center");
}

TEST(notify_zero_opacity_not_serialized) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"notify\",\"notify_text\":\"hi\",\"notify_opacity\":0}");
    StaticJsonDocument<512> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(act, obj);
    ASSERT_TRUE(obj.containsKey("notify_text"));
    ASSERT_TRUE(!obj.containsKey("notify_opacity"));
}

TEST(notify_minimal_only_text) {
    ButtonAction act = round_trip("{\"type\":\"notify\",\"notify_text\":\"hello\"}");
    ASSERT_STR(act.type, "notify");
    ASSERT_STR(act.payload.notify.notify_text, "hello");
    ASSERT_STR(act.payload.notify.notify_duration_ms, "");
    ASSERT_STR(act.payload.notify.notify_text_color, "");
    ASSERT_STR(act.payload.notify.notify_bg_color, "");
    ASSERT_EQ(act.payload.notify.notify_opacity, 0);
    ASSERT_EQ(act.payload.notify.notify_font_size, 0);
    ASSERT_STR(act.payload.notify.notify_location, "");
}

// ============================================================================

// ============================================================================
// System action
// ============================================================================

TEST(system_action_parse) {
    ButtonAction act = parse_from_string("{\"type\":\"system\",\"system_command\":\"reboot\"}");
    ASSERT_STR(act.type, "system");
    ASSERT_STR(act.payload.system.system_command, "reboot");
}

TEST(system_action_round_trip) {
    ButtonAction act = round_trip("{\"type\":\"system\",\"system_command\":\"wifi_reconnect\"}");
    ASSERT_STR(act.type, "system");
    ASSERT_STR(act.payload.system.system_command, "wifi_reconnect");
}

TEST(system_command_not_serialized_when_empty) {
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    strlcpy(act.type, "system", CONFIG_ACTION_TYPE_MAX_LEN);
    // system_command is empty
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(act, obj);
    ASSERT_TRUE(obj.containsKey("type"));
    ASSERT_TRUE(!obj.containsKey("system_command"));
}

TEST(system_action_screensaver) {
    ButtonAction act = round_trip("{\"type\":\"system\",\"system_command\":\"screensaver\"}");
    ASSERT_STR(act.type, "system");
    ASSERT_STR(act.payload.system.system_command, "screensaver");
}

TEST(system_action_wake_display) {
    ButtonAction act = round_trip("{\"type\":\"system\",\"system_command\":\"wake_display\"}");
    ASSERT_STR(act.type, "system");
    ASSERT_STR(act.payload.system.system_command, "wake_display");
}

// ============================================================================
// Visual alert action
// ============================================================================

TEST(visual_alert_action_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"visual_alert\",\"op\":\"start\",\"color\":\"#FF0000\","
        "\"pattern\":\"blink\",\"period_ms\":600,\"intensity\":80,\"duration_ms\":30000}");
    ASSERT_STR(act.type, "visual_alert");
    ASSERT_STR(act.payload.visual_alert.va_op, "start");
    ASSERT_STR(act.payload.visual_alert.va_color, "#FF0000");
    ASSERT_STR(act.payload.visual_alert.va_pattern, "blink");
    ASSERT_EQ(act.payload.visual_alert.va_period_ms, 600);
    ASSERT_EQ(act.payload.visual_alert.va_intensity, 80);
    ASSERT_TRUE(act.payload.visual_alert.va_duration_ms == 30000u);
}

TEST(visual_alert_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"visual_alert\",\"op\":\"stop\",\"pattern\":\"breathe\","
        "\"color\":\"[expr:...]\",\"duration_ms\":0}");
    ASSERT_STR(act.type, "visual_alert");
    ASSERT_STR(act.payload.visual_alert.va_op, "stop");
    ASSERT_STR(act.payload.visual_alert.va_pattern, "breathe");
    ASSERT_STR(act.payload.visual_alert.va_color, "[expr:...]");
    ASSERT_TRUE(act.payload.visual_alert.va_duration_ms == 0u);
}

TEST(visual_alert_zero_fields_not_serialized) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"visual_alert\",\"op\":\"start\",\"period_ms\":0,\"intensity\":0,\"duration_ms\":0}");
    StaticJsonDocument<512> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(act, obj);
    ASSERT_TRUE(obj.containsKey("op"));
    ASSERT_TRUE(!obj.containsKey("period_ms"));
    ASSERT_TRUE(!obj.containsKey("intensity"));
    ASSERT_TRUE(!obj.containsKey("duration_ms"));
}

// ============================================================================
// Cycle pad action
// ============================================================================

TEST(cycle_pad_defaults) {
    ButtonAction act = parse_from_string("{\"type\":\"cycle_pad\"}");
    ASSERT_STR(act.type, "cycle_pad");
    ASSERT_EQ(act.payload.cycle_pad.direction, 1);
    ASSERT_TRUE(act.payload.cycle_pad.wrap);
    ASSERT_TRUE(act.payload.cycle_pad.excluded_mask == 0u);
}

TEST(cycle_pad_previous_no_wrap) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"cycle_pad\",\"direction\":\"previous\",\"wrap\":false}");
    ASSERT_EQ(act.payload.cycle_pad.direction, -1);
    ASSERT_TRUE(!act.payload.cycle_pad.wrap);
}

TEST(cycle_pad_canonical_exclusions) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"cycle_pad\",\"excluded_pads\":\"5, 1,5,bad,99,+2,-3\"}");
    ASSERT_TRUE(act.payload.cycle_pad.excluded_mask == ((1u << 0) | (1u << 4)));

    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(act, obj);
    ASSERT_STR(obj["direction"].as<const char*>(), "next");
    ASSERT_TRUE(obj["wrap"].as<bool>());
    ASSERT_STR(obj["excluded_pads"].as<const char*>(), "1,5");
}

TEST(cycle_pad_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"cycle_pad\",\"direction\":\"previous\",\"wrap\":false,"
        "\"excluded_pads\":\"7,2,7\"}");
    ASSERT_STR(act.type, "cycle_pad");
    ASSERT_EQ(act.payload.cycle_pad.direction, -1);
    ASSERT_TRUE(!act.payload.cycle_pad.wrap);
    ASSERT_TRUE(act.payload.cycle_pad.excluded_mask == ((1u << 1) | (1u << 6)));
}

TEST(cycle_pad_invalid_explicit_fields_clear_action) {
    const char* invalid[] = {
        "{\"type\":\"cycle_pad\",\"direction\":\"sideways\"}",
        "{\"type\":\"cycle_pad\",\"direction\":4}",
        "{\"type\":\"cycle_pad\",\"wrap\":\"true\"}",
        "{\"type\":\"cycle_pad\",\"excluded_pads\":[1,2]}"
    };
    for (const char* json : invalid) {
        ButtonAction act = parse_from_string(json);
        ASSERT_STR(act.type, "");
    }
}

// ============================================================================
// Delay action
// ============================================================================

TEST(delay_action_round_trip) {
    ButtonAction act = round_trip("{\"type\":\"delay\",\"duration_ms\":3000}");
    ASSERT_STR(act.type, "delay");
    ASSERT_EQ(act.payload.delay.duration_ms, 3000);
}

TEST(delay_invalid_duration_clears_action) {
    const char* invalid[] = {
        "{\"type\":\"delay\"}",
        "{\"type\":\"delay\",\"duration_ms\":0}",
        "{\"type\":\"delay\",\"duration_ms\":55001}",
        "{\"type\":\"delay\",\"duration_ms\":\"3000\"}",
        "{\"type\":\"delay\",\"duration_ms\":3.5}"
    };
    for (const char* json : invalid) {
        ButtonAction act = parse_from_string(json);
        ASSERT_STR(act.type, "");
    }
}

TEST(registered_identifiers_survive_storage) {
    ASSERT_TRUE(action_type_count() > 0);
    for (uint8_t index = 0; index < action_type_count(); ++index) {
        const ActionTypeDef* type = action_type_at(index);
        ASSERT_TRUE(strlen(type->type_name) < sizeof(ButtonAction::type));
        ButtonAction stored = {};
        strlcpy(stored.type, type->type_name, sizeof(stored.type));
        ASSERT_STR(stored.type, type->type_name);
        ASSERT_TRUE(action_type_find(stored.type) == type);
    }
}

TEST(catalog_feature_gates_and_metadata_parity) {
    JsonDocument portal;
    action_catalog_emit(portal.to<JsonArray>(), false);
    JsonDocument mcp;
    action_catalog_emit(mcp.to<JsonArray>(), true);
    unsigned alarm_count = 0;
    for (JsonObject entry : mcp.as<JsonArray>()) {
        const char* name = entry["type"];
        const ActionTypeDef* type = action_type_find(name);
        ASSERT_TRUE(type != nullptr);
        ASSERT_EQ(entry["alarm_hook_allowed"].as<bool>(), type->execution == ACTION_EXECUTION_SYNC);
        if (!strcmp(name, "alarm") || !strcmp(name, "alarm_tone") || !strcmp(name, "alarm_mp3")) ++alarm_count;
        entry.remove("fields");
    }
    ASSERT_EQ(alarm_count, ALARM_ENABLED ? 3 : 0);
    ASSERT_TRUE(action_type_find("screen")->display_lock_required);
    ASSERT_TRUE(!action_type_find("ha_service")->display_lock_required);
    std::string portal_json, mcp_json;
    serializeJson(portal, portal_json);
    serializeJson(mcp, mcp_json);
    ASSERT_TRUE(portal_json == mcp_json);
}

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "--catalog")) {
        JsonDocument catalogs;
        action_catalog_emit(catalogs["portal"].to<JsonArray>(), false);
        action_catalog_emit(catalogs["mcp"].to<JsonArray>(), true);
        std::string output;
        serializeJson(catalogs, output);
        std::puts(output.c_str());
        return 0;
    }
    printf("=== ButtonAction Parse/Serialize Tests ===\n\n");

    printf("--- Empty / minimal ---\n");
    RUN(registered_identifiers_survive_storage);
    RUN(catalog_feature_gates_and_metadata_parity);
    RUN(empty_json);
    RUN(empty_to_json_produces_empty_object);
    RUN(action_list_filters_literal_none_for_pad_callers);
    RUN(action_list_retains_literal_none_for_existing_callers);
    RUN(type_only);
    RUN(gamepad_round_trip_and_validation);

    printf("\n--- Screen action ---\n");
    RUN(screen_action_parse);
    RUN(screen_action_round_trip);

    printf("\n--- MQTT action ---\n");
    RUN(mqtt_action_parse);
    RUN(mqtt_action_round_trip);

    printf("\n--- Key action ---\n");
    RUN(key_action_parse);
    RUN(key_action_round_trip);
    RUN(mouse_button_action_round_trip);
    RUN(mouse_button_validation_and_catalog);

    printf("\n--- Music action ---\n");
    RUN(music_action_parse);
    RUN(music_action_round_trip);

    printf("\n--- Sound Alert action ---\n");
    RUN(sound_alert_tone_parse);
    RUN(sound_alert_mp3_round_trip);
    RUN(legacy_alert_aliases_are_rejected);
    RUN(alarm_audio_round_trip);

    printf("\n--- Volume action ---\n");
    RUN(volume_set_action_parse);
    RUN(volume_adjust_action_round_trip);
    RUN(volume_adjust_step_placeholder);

    printf("\n--- Brightness action ---\n");
    RUN(brightness_set_action_parse);
    RUN(brightness_adjust_action_round_trip);
    RUN(brightness_empty_value_not_serialized);
    #if HAS_LVGL_EPAPER
    RUN(display_refresh_action_round_trip);
    #endif

    printf("\n--- Timer action ---\n");
    RUN(timer_action_parse);
    RUN(timer_countdown_start_action_round_trip);
    RUN(timer_oversized_wire_fields_rejected_before_copy);
    RUN(timer_adjust_action_round_trip);
    RUN(timer_set_action_round_trip);
    RUN(timer_fields_serialized_properly);
    RUN(mqtt_payload_serialized_as_payload_not_timer_command);

    printf("\n--- BLE pair action ---\n");
    RUN(ble_pair_action_parse);
    RUN(ble_pair_action_round_trip);

    printf("\n--- Back action ---\n");
    RUN(back_action_round_trip);

    printf("\n--- Edge cases ---\n");
    RUN(missing_optional_fields);
    RUN(compact_serialization);
    RUN(per_type_round_trips_cover_all_arms);

    printf("\n--- Notify action ---\n");
    RUN(notify_action_parse);
    RUN(notify_action_round_trip);
    RUN(notify_zero_opacity_not_serialized);
    RUN(notify_minimal_only_text);

    printf("\n--- System action ---\n");
    RUN(system_action_parse);
    RUN(system_action_round_trip);
    RUN(system_command_not_serialized_when_empty);
    RUN(system_action_screensaver);
    RUN(system_action_wake_display);

    printf("\n--- Visual alert action ---\n");
    RUN(visual_alert_action_parse);
    RUN(visual_alert_action_round_trip);
    RUN(visual_alert_zero_fields_not_serialized);

    printf("\n--- Cycle pad action ---\n");
    RUN(cycle_pad_defaults);
    RUN(cycle_pad_previous_no_wrap);
    RUN(cycle_pad_canonical_exclusions);
    RUN(cycle_pad_round_trip);
    RUN(cycle_pad_invalid_explicit_fields_clear_action);

    printf("\n--- Delay action ---\n");
    RUN(delay_action_round_trip);
    RUN(delay_invalid_duration_clears_action);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
