// ============================================================================
// Unit tests for shutter_session_actions module
// ============================================================================
// Verifies:
//   * JSON parse → serialize → parse round-trip of {save_start_actions: [...],
//     save_complete_actions: [...]} preserves all fields and action counts.
//   * Missing or empty JSON yields default (empty) actions.
//   * Self-trigger guard rejects shutter/sess_stop and shutter/sess_start so
//     a misconfigured action cannot recursively retrigger the save lifecycle.
//
// The full module (shutter_session_actions.cpp) depends on Storage, FreeRTOS,
// and LVGL, so it is not linked here. The pieces under test are the JSON
// shape (array schema via action_parse / action_to_json)
// and the inline self-trigger helper in shutter_session_actions.h.
// ============================================================================

#include <cstdio>
#include <cstring>
#include <ArduinoJson.h>
#include "pad_config.h"
#include "action_parse.h"
#include "action_registry.h"
#include "shutter_session_actions.h"

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

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    assertion failed: %s\n", #cond); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

// ----------------------------------------------------------------------------
// Mirrors the on-disk schema:
//   { save_start_actions: [...], save_complete_actions: [...] }
// ----------------------------------------------------------------------------
struct TestConfig {
    ButtonAction save_start_actions[MAX_BUTTON_ACTIONS];
    uint8_t      save_start_count;
    ButtonAction save_complete_actions[MAX_BUTTON_ACTIONS];
    uint8_t      save_complete_count;
};

static uint8_t parse_actions_var(JsonVariant v, ButtonAction out[MAX_BUTTON_ACTIONS]) {
    memset(out, 0, sizeof(ButtonAction) * MAX_BUTTON_ACTIONS);
    uint8_t count = 0;
    if (!v.is<JsonArray>()) return 0;
    JsonArray arr = v.as<JsonArray>();
    for (size_t i = 0; i < arr.size() && count < MAX_BUTTON_ACTIONS; i++) {
        if (!arr[i].is<JsonObject>()) continue;
        action_parse(arr[i].as<JsonObject>(), out[count]);
        if (out[count].type[0]) count++;
        else memset(&out[count], 0, sizeof(ButtonAction));
    }
    return count;
}

static void parse_config(const char* json_str, TestConfig& cfg) {
    memset(&cfg, 0, sizeof(cfg));
    StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, json_str) != DeserializationError::Ok) return;
    JsonObject root = doc.as<JsonObject>();
    cfg.save_start_count    = parse_actions_var(root["save_start_actions"],    cfg.save_start_actions);
    cfg.save_complete_count = parse_actions_var(root["save_complete_actions"], cfg.save_complete_actions);
}

static void serialize_config(const TestConfig& cfg, char* out, size_t out_len) {
    StaticJsonDocument<2048> doc;
    JsonObject root = doc.to<JsonObject>();
    JsonArray sa = root.createNestedArray("save_start_actions");
    for (uint8_t i = 0; i < cfg.save_start_count; i++) {
        JsonObject o = sa.createNestedObject();
        action_to_json(cfg.save_start_actions[i], o);
    }
    JsonArray ca = root.createNestedArray("save_complete_actions");
    for (uint8_t i = 0; i < cfg.save_complete_count; i++) {
        JsonObject o = ca.createNestedObject();
        action_to_json(cfg.save_complete_actions[i], o);
    }
    serializeJson(doc, out, out_len);
}

// ============================================================================
// Defaults
// ============================================================================

TEST(missing_file_defaults_to_empty) {
    TestConfig cfg;
    parse_config("{}", cfg);
    ASSERT_TRUE(cfg.save_start_count == 0);
    ASSERT_TRUE(cfg.save_complete_count == 0);
}

TEST(empty_actions_round_trip_to_empty) {
    TestConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    char buf[1024];
    serialize_config(cfg, buf, sizeof(buf));
    TestConfig back;
    parse_config(buf, back);
    ASSERT_TRUE(back.save_start_count == 0);
    ASSERT_TRUE(back.save_complete_count == 0);
}

// ============================================================================
// Round-trip
// ============================================================================

TEST(round_trip_preserves_both_actions) {
    const char* json =
        "{\"save_start_actions\":[{\"type\":\"notify\",\"notify_text\":\"Saving...\"}],"
         "\"save_complete_actions\":[{\"type\":\"sound_alert\",\"sound_alert_kind\":\"tone\",\"sound_alert_pattern\":\"100\",\"sound_alert_volume\":50}]}";
    TestConfig cfg;
    parse_config(json, cfg);
    ASSERT_TRUE(cfg.save_start_count == 1);
    ASSERT_TRUE(cfg.save_complete_count == 1);
    ASSERT_STR(cfg.save_start_actions[0].type, "notify");
    ASSERT_STR(cfg.save_start_actions[0].payload.notify.notify_text, "Saving...");
    ASSERT_STR(cfg.save_complete_actions[0].type, "sound_alert");
    ASSERT_STR(cfg.save_complete_actions[0].payload.sound_alert.sound_alert_pattern, "100");

    char buf[1024];
    serialize_config(cfg, buf, sizeof(buf));

    TestConfig back;
    parse_config(buf, back);
    ASSERT_TRUE(back.save_start_count == 1);
    ASSERT_STR(back.save_start_actions[0].type, "notify");
    ASSERT_STR(back.save_start_actions[0].payload.notify.notify_text, "Saving...");
    ASSERT_STR(back.save_complete_actions[0].type, "sound_alert");
    ASSERT_STR(back.save_complete_actions[0].payload.sound_alert.sound_alert_pattern, "100");
}

TEST(round_trip_screen_action) {
    const char* json =
        "{\"save_start_actions\":[{\"type\":\"screen\",\"target\":\"pad_1\"}],"
         "\"save_complete_actions\":[]}";
    TestConfig cfg;
    parse_config(json, cfg);
    char buf[1024];
    serialize_config(cfg, buf, sizeof(buf));
    TestConfig back;
    parse_config(buf, back);
    ASSERT_TRUE(back.save_start_count == 1);
    ASSERT_STR(back.save_start_actions[0].type, "screen");
    ASSERT_STR(back.save_start_actions[0].payload.screen.screen_id, "pad_1");
    ASSERT_TRUE(back.save_complete_count == 0);
}

TEST(round_trip_three_actions_per_event) {
    const char* json =
        "{\"save_start_actions\":["
            "{\"type\":\"notify\",\"notify_text\":\"A\"},"
            "{\"type\":\"sound_alert\",\"sound_alert_kind\":\"tone\",\"sound_alert_pattern\":\"50\",\"sound_alert_volume\":30},"
            "{\"type\":\"screen\",\"target\":\"pad_2\"}"
         "],\"save_complete_actions\":["
            "{\"type\":\"notify\",\"notify_text\":\"X\"},"
            "{\"type\":\"notify\",\"notify_text\":\"Y\"},"
            "{\"type\":\"notify\",\"notify_text\":\"Z\"}"
         "]}";
    TestConfig cfg;
    parse_config(json, cfg);
    ASSERT_TRUE(cfg.save_start_count == 3);
    ASSERT_TRUE(cfg.save_complete_count == 3);
    ASSERT_STR(cfg.save_start_actions[0].type, "notify");
    ASSERT_STR(cfg.save_start_actions[1].type, "sound_alert");
    ASSERT_STR(cfg.save_start_actions[2].type, "screen");
    ASSERT_STR(cfg.save_complete_actions[2].payload.notify.notify_text, "Z");

    char buf[2048];
    serialize_config(cfg, buf, sizeof(buf));
    TestConfig back;
    parse_config(buf, back);
    ASSERT_TRUE(back.save_start_count == 3);
    ASSERT_TRUE(back.save_complete_count == 3);
    ASSERT_STR(back.save_start_actions[1].payload.sound_alert.sound_alert_pattern, "50");
    ASSERT_STR(back.save_complete_actions[0].payload.notify.notify_text, "X");
}

TEST(parse_drops_empty_array_entries) {
    const char* json =
        "{\"save_start_actions\":[{},{\"type\":\"notify\",\"notify_text\":\"A\"},{}]}";
    TestConfig cfg;
    parse_config(json, cfg);
    ASSERT_TRUE(cfg.save_start_count == 1);
    ASSERT_STR(cfg.save_start_actions[0].type, "notify");
    ASSERT_STR(cfg.save_start_actions[0].payload.notify.notify_text, "A");
}

// ============================================================================
// Self-trigger guard
// ============================================================================

TEST(self_trigger_rejects_sess_stop) {
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    strcpy(act.type, ACTION_TYPE_SHUTTER);
    strcpy(shutter_payload(act).command, "sess_stop");
    ASSERT_TRUE(shutter_session_actions_is_self_trigger(act));
}

TEST(self_trigger_rejects_sess_start) {
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    strcpy(act.type, ACTION_TYPE_SHUTTER);
    strcpy(shutter_payload(act).command, "sess_start");
    ASSERT_TRUE(shutter_session_actions_is_self_trigger(act));
}

TEST(self_trigger_allows_other_shutter_commands) {
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    strcpy(act.type, ACTION_TYPE_SHUTTER);
    strcpy(shutter_payload(act).command, "set");
    ASSERT_FALSE(shutter_session_actions_is_self_trigger(act));
}

TEST(self_trigger_allows_non_shutter_action) {
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    strcpy(act.type, "notify");
    // device_class slot is not the active arm here; the function must check
    // act.type first and ignore payload contents.
    ASSERT_FALSE(shutter_session_actions_is_self_trigger(act));
}

TEST(self_trigger_allows_empty_action) {
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    ASSERT_FALSE(shutter_session_actions_is_self_trigger(act));
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("\n=== shutter_session_actions tests ===\n");

    printf("\n--- Defaults ---\n");
    RUN(missing_file_defaults_to_empty);
    RUN(empty_actions_round_trip_to_empty);

    printf("\n--- Round-trip ---\n");
    RUN(round_trip_preserves_both_actions);
    RUN(round_trip_screen_action);
    RUN(round_trip_three_actions_per_event);
    RUN(parse_drops_empty_array_entries);

    printf("\n--- Self-trigger guard ---\n");
    RUN(self_trigger_rejects_sess_stop);
    RUN(self_trigger_rejects_sess_start);
    RUN(self_trigger_allows_other_shutter_commands);
    RUN(self_trigger_allows_non_shutter_action);
    RUN(self_trigger_allows_empty_action);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
