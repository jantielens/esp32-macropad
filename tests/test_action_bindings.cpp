// ============================================================================
// Unit tests for action binding resolution — verifies that binding template
// resolution works correctly on ButtonAction value fields
// ============================================================================
// Host-native: tests the binding engine behavior on action-field-sized buffers.
// Exercises the same resolve pattern used by resolve_action_bindings() in
// action_dispatch.cpp without requiring ESP32-specific headers.

#include <cstdio>
#include <cstring>

#include "binding_template.h"
#include "pad_config.h"

// ---------------------------------------------------------------------------
// Mock resolvers
// ---------------------------------------------------------------------------
static bool mock_resolve(const char* params, char* out, size_t out_len) {
    (void)params;
    strlcpy(out, "RESOLVED", out_len);
    return true;
}

static bool mock_echo(const char* params, char* out, size_t out_len) {
    strlcpy(out, params, out_len);
    return true;
}

// Returns a long string to test field truncation
static bool mock_long(const char* params, char* out, size_t out_len) {
    (void)params;
    strlcpy(out, "1234567890123456789", out_len);
    return true;
}

static void mock_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ---------------------------------------------------------------------------
// Reproduce the resolve_action_bindings pattern for testing
// ---------------------------------------------------------------------------
// This matches the logic in action_dispatch.cpp — resolve value-class fields
// in-place, skip structural fields.
static void test_try_resolve(char* field, size_t len) {
    if (field[0] && binding_template_has_bindings(field)) {
        char tmp[BINDING_TEMPLATE_MAX_LEN];
        binding_template_resolve(field, tmp, sizeof(tmp));
        strlcpy(field, tmp, len);
    }
}

static void resolve_action_bindings(ButtonAction& act) {
    test_try_resolve(act.mqtt_topic,          sizeof(act.mqtt_topic));
    test_try_resolve(act.mqtt_payload,        sizeof(act.mqtt_payload));
    test_try_resolve(act.key_sequence,        sizeof(act.key_sequence));
    test_try_resolve(act.beep_pattern,        sizeof(act.beep_pattern));
    test_try_resolve(act.volume_value,        sizeof(act.volume_value));
    test_try_resolve(act.brightness_value,    sizeof(act.brightness_value));
    test_try_resolve(act.timer_value,         sizeof(act.timer_value));
    test_try_resolve(act.notify_text,         sizeof(act.notify_text));
    test_try_resolve(act.notify_duration_ms,  sizeof(act.notify_duration_ms));
    test_try_resolve(act.notify_text_color,   sizeof(act.notify_text_color));
    test_try_resolve(act.notify_bg_color,     sizeof(act.notify_bg_color));
    test_try_resolve(act.notify_border_color, sizeof(act.notify_border_color));
}

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------
static int g_pass = 0;
static int g_fail = 0;

static void check_str(const char* got, const char* expected, const char* label) {
    if (strcmp(got, expected) != 0) {
        printf("  FAIL [%s]\n    got:      \"%s\"\n    expected: \"%s\"\n", label, got, expected);
        g_fail++;
        return;
    }
    g_pass++;
}

static void check_true(bool cond, const char* label) {
    if (!cond) {
        printf("  FAIL [%s]\n", label);
        g_fail++;
        return;
    }
    g_pass++;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_value_fields_resolved() {
    printf("--- value fields are resolved ---\n");
    ButtonAction act = {};
    strlcpy(act.mqtt_topic,          "[mock:t]", sizeof(act.mqtt_topic));
    strlcpy(act.mqtt_payload,        "[mock:p]", sizeof(act.mqtt_payload));
    strlcpy(act.key_sequence,        "[mock:k]", sizeof(act.key_sequence));
    strlcpy(act.beep_pattern,        "[mock:b]", sizeof(act.beep_pattern));
    strlcpy(act.volume_value,        "[mock:v]", sizeof(act.volume_value));
    strlcpy(act.brightness_value,    "[mock:br]", sizeof(act.brightness_value));
    strlcpy(act.timer_value,         "[mock:tv]", sizeof(act.timer_value));
    strlcpy(act.notify_text,         "[mock:nt]", sizeof(act.notify_text));
    strlcpy(act.notify_duration_ms,  "[mock:nd]", sizeof(act.notify_duration_ms));
    strlcpy(act.notify_text_color,   "[mock:tc]", sizeof(act.notify_text_color));
    strlcpy(act.notify_bg_color,     "[mock:bg]", sizeof(act.notify_bg_color));
    strlcpy(act.notify_border_color, "[mock:bc]", sizeof(act.notify_border_color));

    resolve_action_bindings(act);

    check_str(act.mqtt_topic,          "RESOLVED", "mqtt_topic resolved");
    check_str(act.mqtt_payload,        "RESOLVED", "mqtt_payload resolved");
    check_str(act.key_sequence,        "RESOLVED", "key_sequence resolved");
    check_str(act.beep_pattern,        "RESOLVED", "beep_pattern resolved");
    check_str(act.volume_value,        "RESOLVED", "volume_value resolved");
    check_str(act.brightness_value,    "RESOLVED", "brightness_value resolved");
    check_str(act.timer_value,         "RESOLVED", "timer_value resolved");
    check_str(act.notify_text,         "RESOLVED", "notify_text resolved");
    check_str(act.notify_duration_ms,  "RESOLVED", "notify_duration_ms resolved");
    check_str(act.notify_text_color,   "RESOLVED", "notify_text_color resolved");
    check_str(act.notify_bg_color,     "RESOLVED", "notify_bg_color resolved");
    check_str(act.notify_border_color, "RESOLVED", "notify_border_color resolved");
}

static void test_structural_fields_excluded() {
    printf("--- structural fields are NOT resolved ---\n");
    ButtonAction act = {};
    strlcpy(act.type,           "[mock:x]", sizeof(act.type));
    strlcpy(act.screen_id,      "[mock:x]", sizeof(act.screen_id));
    // These fields are only 8 bytes — use a short token that fits
    strlcpy(act.volume_mode,    "[m:x]",    sizeof(act.volume_mode));
    strlcpy(act.brightness_mode,"[m:x]",    sizeof(act.brightness_mode));
    strlcpy(act.timer_command,  "[mock:x]", sizeof(act.timer_command));
    strlcpy(act.system_command, "[mock:x]", sizeof(act.system_command));
    strlcpy(act.notify_location,"[m:x]",    sizeof(act.notify_location));
    strlcpy(act.sound_file,     "[mock:x]", sizeof(act.sound_file));

    resolve_action_bindings(act);

    check_str(act.type,            "[mock:x]", "type not resolved");
    check_str(act.screen_id,       "[mock:x]", "screen_id not resolved");
    check_str(act.volume_mode,     "[m:x]",    "volume_mode not resolved");
    check_str(act.brightness_mode, "[m:x]",    "brightness_mode not resolved");
    check_str(act.timer_command,   "[mock:x]", "timer_command not resolved");
    check_str(act.system_command,  "[mock:x]", "system_command not resolved");
    check_str(act.notify_location, "[m:x]",    "notify_location not resolved");
    check_str(act.sound_file,      "[mock:x]", "sound_file not resolved");
}

static void test_plain_values_unchanged() {
    printf("--- plain values without bindings stay unchanged ---\n");
    ButtonAction act = {};
    strlcpy(act.mqtt_topic,   "home/light/set", sizeof(act.mqtt_topic));
    strlcpy(act.mqtt_payload, "ON",             sizeof(act.mqtt_payload));
    strlcpy(act.timer_value,  "300",            sizeof(act.timer_value));

    resolve_action_bindings(act);

    check_str(act.mqtt_topic,   "home/light/set", "plain mqtt_topic unchanged");
    check_str(act.mqtt_payload, "ON",              "plain mqtt_payload unchanged");
    check_str(act.timer_value,  "300",             "plain timer_value unchanged");
}

static void test_empty_fields_stay_empty() {
    printf("--- empty fields stay empty ---\n");
    ButtonAction act = {};

    resolve_action_bindings(act);

    check_str(act.mqtt_topic,          "", "empty mqtt_topic stays empty");
    check_str(act.mqtt_payload,        "", "empty mqtt_payload stays empty");
    check_str(act.notify_text,         "", "empty notify_text stays empty");
    check_str(act.notify_duration_ms,  "", "empty notify_duration_ms stays empty");
}

static void test_curly_brace_step_not_affected() {
    printf("--- {step} placeholder is not affected ---\n");
    ButtonAction act = {};
    strlcpy(act.volume_value,     "{step}", sizeof(act.volume_value));
    strlcpy(act.brightness_value, "{step}", sizeof(act.brightness_value));
    strlcpy(act.timer_value,      "{step}", sizeof(act.timer_value));

    resolve_action_bindings(act);

    check_str(act.volume_value,     "{step}", "{step} in volume_value unchanged");
    check_str(act.brightness_value, "{step}", "{step} in brightness_value unchanged");
    check_str(act.timer_value,      "{step}", "{step} in timer_value unchanged");
}

static void test_mixed_static_and_binding() {
    printf("--- mixed static text and binding token ---\n");
    ButtonAction act = {};
    strlcpy(act.notify_text, "CPU: [echo:42]%", sizeof(act.notify_text));

    resolve_action_bindings(act);

    check_str(act.notify_text, "CPU: 42%", "mixed binding resolved");
}

static void test_small_field_truncation() {
    printf("--- small field truncation on resolve ---\n");
    ButtonAction act = {};
    // volume_value is CONFIG_VALUE_MAX_LEN = 16 bytes
    // [long:x] fits in 16 bytes; resolver returns 19-char string, truncated to 15+nul
    strlcpy(act.volume_value, "[long:x]", sizeof(act.volume_value));

    resolve_action_bindings(act);

    check_true(strlen(act.volume_value) == CONFIG_VALUE_MAX_LEN - 1,
               "truncated to field size");
    check_str(act.volume_value, "123456789012345", "truncated content correct");
}

static void test_unregistered_scheme() {
    printf("--- unregistered scheme produces error marker ---\n");
    ButtonAction act = {};
    strlcpy(act.mqtt_payload, "[nosuch:key]", sizeof(act.mqtt_payload));

    resolve_action_bindings(act);

    check_str(act.mqtt_payload, "ERR:unknown", "unregistered scheme produces error");
}

static void test_multiple_bindings_in_one_field() {
    printf("--- multiple binding tokens in one field ---\n");
    ButtonAction act = {};
    strlcpy(act.notify_text, "[echo:hello] [echo:world]", sizeof(act.notify_text));

    resolve_action_bindings(act);

    check_str(act.notify_text, "hello world", "multiple bindings resolved");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    printf("=== resolve_action_bindings tests ===\n\n");

    binding_template_register("mock", mock_resolve, mock_collect);
    binding_template_register("echo", mock_echo, mock_collect);
    binding_template_register("long", mock_long, mock_collect);
    // Register "m" as alias for mock (short scheme name for small-field tests)
    binding_template_register("m", mock_resolve, mock_collect);

    test_value_fields_resolved();
    test_structural_fields_excluded();
    test_plain_values_unchanged();
    test_empty_fields_stay_empty();
    test_curly_brace_step_not_affected();
    test_mixed_static_and_binding();
    test_small_field_truncation();
    test_unregistered_scheme();
    test_multiple_bindings_in_one_field();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
