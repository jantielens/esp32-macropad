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
#include "pad_config.h"
#include "action_parse.h"

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
    ASSERT_STR(act.screen_id, "");
    ASSERT_STR(act.mqtt_topic, "");
    ASSERT_EQ(act.beep_volume, 0);
    ASSERT_EQ(act.sound_volume, 0);
}

TEST(empty_to_json_produces_empty_object) {
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(act, obj);
    ASSERT_EQ(obj.size(), 0);  // empty action → no keys
}

TEST(type_only) {
    ButtonAction act = parse_from_string("{\"type\":\"back\"}");
    ASSERT_STR(act.type, "back");
    ASSERT_STR(act.screen_id, "");
}

// ============================================================================
// Screen action
// ============================================================================

TEST(screen_action_parse) {
    ButtonAction act = parse_from_string("{\"type\":\"screen\",\"target\":\"pad_3\"}");
    ASSERT_STR(act.type, "screen");
    ASSERT_STR(act.screen_id, "pad_3");
}

TEST(screen_action_round_trip) {
    ButtonAction act = round_trip("{\"type\":\"screen\",\"target\":\"pad_0\"}");
    ASSERT_STR(act.type, "screen");
    ASSERT_STR(act.screen_id, "pad_0");
}

// ============================================================================
// MQTT action
// ============================================================================

TEST(mqtt_action_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"mqtt\",\"topic\":\"home/light\",\"payload\":\"ON\"}");
    ASSERT_STR(act.type, "mqtt");
    ASSERT_STR(act.mqtt_topic, "home/light");
    ASSERT_STR(act.mqtt_payload, "ON");
}

TEST(mqtt_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"mqtt\",\"topic\":\"home/light\",\"payload\":\"toggle\"}");
    ASSERT_STR(act.type, "mqtt");
    ASSERT_STR(act.mqtt_topic, "home/light");
    ASSERT_STR(act.mqtt_payload, "toggle");
}

// ============================================================================
// Key action
// ============================================================================

TEST(key_action_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"key\",\"sequence\":\"Ctrl+C\"}");
    ASSERT_STR(act.type, "key");
    ASSERT_STR(act.key_sequence, "Ctrl+C");
}

TEST(key_action_round_trip) {
    ButtonAction act = round_trip("{\"type\":\"key\",\"sequence\":\"Alt+Tab\"}");
    ASSERT_STR(act.type, "key");
    ASSERT_STR(act.key_sequence, "Alt+Tab");
}

// ============================================================================
// Beep action
// ============================================================================

TEST(beep_action_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"beep\",\"beep_pattern\":\"1000:200 100 500:300\",\"beep_volume\":75}");
    ASSERT_STR(act.type, "beep");
    ASSERT_STR(act.beep_pattern, "1000:200 100 500:300");
    ASSERT_EQ(act.beep_volume, 75);
}

TEST(beep_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"beep\",\"beep_pattern\":\"440:500\",\"beep_volume\":50}");
    ASSERT_STR(act.type, "beep");
    ASSERT_STR(act.beep_pattern, "440:500");
    ASSERT_EQ(act.beep_volume, 50);
}

TEST(beep_zero_volume_not_serialized) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"beep\",\"beep_pattern\":\"1000:100\",\"beep_volume\":0}");
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(act, obj);
    ASSERT_TRUE(!obj.containsKey("beep_volume"));
    ASSERT_TRUE(obj.containsKey("beep_pattern"));
}

// ============================================================================
// Sound action
// ============================================================================

TEST(sound_action_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"sound\",\"sound_file\":\"doorbell\",\"sound_volume\":80}");
    ASSERT_STR(act.type, "sound");
    ASSERT_STR(act.sound_file, "doorbell");
    ASSERT_EQ(act.sound_volume, 80);
}

TEST(sound_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"sound\",\"sound_file\":\"startup\",\"sound_volume\":100}");
    ASSERT_STR(act.type, "sound");
    ASSERT_STR(act.sound_file, "startup");
    ASSERT_EQ(act.sound_volume, 100);
}

TEST(sound_zero_volume_not_serialized) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"sound\",\"sound_file\":\"chime\",\"sound_volume\":0}");
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(act, obj);
    ASSERT_TRUE(!obj.containsKey("sound_volume"));
    ASSERT_TRUE(obj.containsKey("sound_file"));
}

// ============================================================================
// Volume action
// ============================================================================

TEST(volume_action_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"volume\",\"volume_mode\":\"set\",\"volume_value\":60}");
    ASSERT_STR(act.type, "volume");
    ASSERT_STR(act.volume_mode, "set");
    ASSERT_EQ(act.volume_value, 60);
}

TEST(volume_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"volume\",\"volume_mode\":\"up\",\"volume_value\":10}");
    ASSERT_STR(act.type, "volume");
    ASSERT_STR(act.volume_mode, "up");
    ASSERT_EQ(act.volume_value, 10);
}

// ============================================================================
// Brightness action
// ============================================================================

TEST(brightness_action_parse) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"brightness\",\"brightness_mode\":\"set\",\"brightness_value\":75}");
    ASSERT_STR(act.type, "brightness");
    ASSERT_STR(act.brightness_mode, "set");
    ASSERT_EQ(act.brightness_value, 75);
}

TEST(brightness_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"brightness\",\"brightness_mode\":\"up\",\"brightness_value\":20}");
    ASSERT_STR(act.type, "brightness");
    ASSERT_STR(act.brightness_mode, "up");
    ASSERT_EQ(act.brightness_value, 20);
}

TEST(brightness_zero_value_not_serialized) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"brightness\",\"brightness_mode\":\"down\",\"brightness_value\":0}");
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
        "{\"type\":\"timer\",\"timer_command\":\"toggle 0\"}");
    ASSERT_STR(act.type, "timer");
    ASSERT_STR(act.mqtt_payload, "toggle 0");  // timer_command maps to mqtt_payload
}

TEST(timer_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"timer\",\"timer_command\":\"start 1\"}");
    ASSERT_STR(act.type, "timer");
    ASSERT_STR(act.mqtt_payload, "start 1");
}

TEST(timer_command_serialized_as_timer_command_not_payload) {
    ButtonAction act = parse_from_string(
        "{\"type\":\"timer\",\"timer_command\":\"toggle 2\"}");
    StaticJsonDocument<256> doc;
    JsonObject obj = doc.to<JsonObject>();
    action_to_json(act, obj);
    ASSERT_TRUE(obj.containsKey("timer_command"));
    ASSERT_TRUE(!obj.containsKey("payload"));
    ASSERT_STR(obj["timer_command"].as<const char*>(), "toggle 2");
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
    ButtonAction act = parse_from_string("{\"type\":\"beep\"}");
    ASSERT_STR(act.beep_pattern, "");
    ASSERT_EQ(act.beep_volume, 0);
    ASSERT_STR(act.sound_file, "");
    ASSERT_EQ(act.sound_volume, 0);
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

TEST(all_fields_populated) {
    // A contrived action with every field set (not a realistic combo, but tests completeness)
    const char* json =
        "{\"type\":\"mqtt\",\"target\":\"pad_1\",\"topic\":\"t\",\"payload\":\"p\","
        "\"sequence\":\"Ctrl+A\",\"beep_pattern\":\"500:100\",\"beep_volume\":42,"
        "\"volume_mode\":\"set\",\"volume_value\":55,"
        "\"brightness_mode\":\"up\",\"brightness_value\":15,"
        "\"sound_file\":\"alert\",\"sound_volume\":90,"
        "\"notify_text\":\"hello\",\"notify_duration_ms\":\"5000\","
        "\"notify_text_color\":\"#ff0000\",\"notify_bg_color\":\"#00ff00\","
        "\"notify_border_color\":\"#0000ff\",\"notify_opacity\":90,"
        "\"notify_font_size\":24,\"notify_location\":\"top\"}";
    ButtonAction act = round_trip(json);
    ASSERT_STR(act.type, "mqtt");
    ASSERT_STR(act.screen_id, "pad_1");
    ASSERT_STR(act.mqtt_topic, "t");
    ASSERT_STR(act.mqtt_payload, "p");
    ASSERT_STR(act.key_sequence, "Ctrl+A");
    ASSERT_STR(act.beep_pattern, "500:100");
    ASSERT_EQ(act.beep_volume, 42);
    ASSERT_STR(act.volume_mode, "set");
    ASSERT_EQ(act.volume_value, 55);
    ASSERT_STR(act.brightness_mode, "up");
    ASSERT_EQ(act.brightness_value, 15);
    ASSERT_STR(act.sound_file, "alert");
    ASSERT_EQ(act.sound_volume, 90);
    ASSERT_STR(act.notify_text, "hello");
    ASSERT_STR(act.notify_duration_ms, "5000");
    ASSERT_STR(act.notify_text_color, "#ff0000");
    ASSERT_STR(act.notify_bg_color, "#00ff00");
    ASSERT_STR(act.notify_border_color, "#0000ff");
    ASSERT_EQ(act.notify_opacity, 90);
    ASSERT_EQ(act.notify_font_size, 24);
    ASSERT_STR(act.notify_location, "top");
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
    ASSERT_STR(act.notify_text, "Power high!");
    ASSERT_STR(act.notify_duration_ms, "5000");
    ASSERT_STR(act.notify_text_color, "#ffa0a0");
    ASSERT_STR(act.notify_bg_color, "#2e1a1a");
    ASSERT_STR(act.notify_border_color, "#5a2a2a");
    ASSERT_EQ(act.notify_opacity, 90);
    ASSERT_EQ(act.notify_font_size, 18);
    ASSERT_STR(act.notify_location, "top");
}

TEST(notify_action_round_trip) {
    ButtonAction act = round_trip(
        "{\"type\":\"notify\",\"notify_text\":\"Test msg\","
        "\"notify_duration_ms\":\"0\",\"notify_bg_color\":\"#333333\","
        "\"notify_location\":\"center\"}");
    ASSERT_STR(act.type, "notify");
    ASSERT_STR(act.notify_text, "Test msg");
    ASSERT_STR(act.notify_duration_ms, "0");
    ASSERT_STR(act.notify_bg_color, "#333333");
    ASSERT_STR(act.notify_location, "center");
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
    ASSERT_STR(act.notify_text, "hello");
    ASSERT_STR(act.notify_duration_ms, "");
    ASSERT_STR(act.notify_text_color, "");
    ASSERT_STR(act.notify_bg_color, "");
    ASSERT_EQ(act.notify_opacity, 0);
    ASSERT_EQ(act.notify_font_size, 0);
    ASSERT_STR(act.notify_location, "");
}

// ============================================================================

int main() {
    printf("=== ButtonAction Parse/Serialize Tests ===\n\n");

    printf("--- Empty / minimal ---\n");
    RUN(empty_json);
    RUN(empty_to_json_produces_empty_object);
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

    printf("\n--- Beep action ---\n");
    RUN(beep_action_parse);
    RUN(beep_action_round_trip);
    RUN(beep_zero_volume_not_serialized);

    printf("\n--- Sound action ---\n");
    RUN(sound_action_parse);
    RUN(sound_action_round_trip);
    RUN(sound_zero_volume_not_serialized);

    printf("\n--- Volume action ---\n");
    RUN(volume_action_parse);
    RUN(volume_action_round_trip);

    printf("\n--- Brightness action ---\n");
    RUN(brightness_action_parse);
    RUN(brightness_action_round_trip);
    RUN(brightness_zero_value_not_serialized);

    printf("\n--- Timer action ---\n");
    RUN(timer_action_parse);
    RUN(timer_action_round_trip);
    RUN(timer_command_serialized_as_timer_command_not_payload);
    RUN(mqtt_payload_serialized_as_payload_not_timer_command);

    printf("\n--- BLE pair action ---\n");
    RUN(ble_pair_action_parse);
    RUN(ble_pair_action_round_trip);

    printf("\n--- Back action ---\n");
    RUN(back_action_round_trip);

    printf("\n--- Edge cases ---\n");
    RUN(missing_optional_fields);
    RUN(compact_serialization);
    RUN(all_fields_populated);

    printf("\n--- Notify action ---\n");
    RUN(notify_action_parse);
    RUN(notify_action_round_trip);
    RUN(notify_zero_opacity_not_serialized);
    RUN(notify_minimal_only_text);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
