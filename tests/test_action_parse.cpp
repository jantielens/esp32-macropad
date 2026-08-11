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

int main() {
    printf("=== ButtonAction Parse/Serialize Tests ===\n\n");

    printf("--- Empty / minimal ---\n");
    RUN(empty_json);
    RUN(empty_to_json_produces_empty_object);
    RUN(action_list_filters_literal_none_for_pad_callers);
    RUN(action_list_retains_literal_none_for_existing_callers);
    RUN(type_only);

    printf("\n--- Screen action ---\n");
    RUN(screen_action_parse);
    RUN(screen_action_round_trip);

    printf("\n--- MQTT action ---\n");
    RUN(mqtt_action_parse);
    RUN(mqtt_action_round_trip);

    printf("\n--- Key action ---\n");
    RUN(key_action_parse);
    RUN(key_action_round_trip);

    printf("\n--- Music action ---\n");
    RUN(music_action_parse);
    RUN(music_action_round_trip);

    printf("\n--- Sound Alert action ---\n");
    RUN(sound_alert_tone_parse);
    RUN(sound_alert_mp3_round_trip);
    RUN(legacy_alert_aliases_are_rejected);

    printf("\n--- Volume action ---\n");
    RUN(volume_set_action_parse);
    RUN(volume_adjust_action_round_trip);
    RUN(volume_adjust_step_placeholder);

    printf("\n--- Brightness action ---\n");
    RUN(brightness_set_action_parse);
    RUN(brightness_adjust_action_round_trip);
    RUN(brightness_empty_value_not_serialized);

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
