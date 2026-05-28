// ============================================================================
// Unit tests for action binding resolution — verifies that binding template
// resolution works correctly on ButtonAction value fields
// ============================================================================
// Host-native: tests the binding engine behavior on action-field-sized buffers.
// Exercises the same resolve pattern used by resolve_action_bindings() in
// action_dispatch.cpp without requiring ESP32-specific headers.
//
// ButtonAction is a discriminated union; each test sets exactly one arm
// (selected by `act.type`) before invoking the type-dispatched resolver.

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
// Type-dispatched resolver — mirrors action_dispatch.cpp::resolve_action_bindings
// ---------------------------------------------------------------------------
static void test_try_resolve(char* field, size_t len) {
    if (field[0] && binding_template_has_bindings(field)) {
        char tmp[BINDING_TEMPLATE_MAX_LEN];
        binding_template_resolve(field, tmp, sizeof(tmp));
        strlcpy(field, tmp, len);
    }
}

static void resolve_action_bindings(ButtonAction& act) {
    if (strcmp(act.type, ACTION_TYPE_SCREEN) == 0) {
        test_try_resolve(act.payload.screen.screen_id, sizeof(act.payload.screen.screen_id));
    } else if (strcmp(act.type, ACTION_TYPE_MQTT) == 0) {
        test_try_resolve(act.payload.mqtt.mqtt_topic,   sizeof(act.payload.mqtt.mqtt_topic));
        test_try_resolve(act.payload.mqtt.mqtt_payload, sizeof(act.payload.mqtt.mqtt_payload));
    } else if (strcmp(act.type, ACTION_TYPE_KEY) == 0) {
        test_try_resolve(act.payload.key.key_sequence, sizeof(act.payload.key.key_sequence));
    } else if (strcmp(act.type, ACTION_TYPE_BEEP) == 0) {
        test_try_resolve(act.payload.beep.beep_pattern, sizeof(act.payload.beep.beep_pattern));
    } else if (strcmp(act.type, ACTION_TYPE_VOLUME) == 0) {
        test_try_resolve(act.payload.volume.volume_value, sizeof(act.payload.volume.volume_value));
    } else if (strcmp(act.type, ACTION_TYPE_BRIGHTNESS) == 0) {
        test_try_resolve(act.payload.brightness.brightness_value, sizeof(act.payload.brightness.brightness_value));
    } else if (strcmp(act.type, ACTION_TYPE_TIMER) == 0) {
        test_try_resolve(act.payload.timer.timer_value, sizeof(act.payload.timer.timer_value));
    } else if (strcmp(act.type, ACTION_TYPE_NOTIFY) == 0) {
        test_try_resolve(act.payload.notify.notify_text,         sizeof(act.payload.notify.notify_text));
        test_try_resolve(act.payload.notify.notify_duration_ms,  sizeof(act.payload.notify.notify_duration_ms));
        test_try_resolve(act.payload.notify.notify_text_color,   sizeof(act.payload.notify.notify_text_color));
        test_try_resolve(act.payload.notify.notify_bg_color,     sizeof(act.payload.notify.notify_bg_color));
        test_try_resolve(act.payload.notify.notify_border_color, sizeof(act.payload.notify.notify_border_color));
    }
    // sound, system, back, ble_pair: no bindable fields today.
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

// Helper: zero-init a ButtonAction and set its discriminator.
static ButtonAction make_action(const char* type) {
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    strlcpy(act.type, type, sizeof(act.type));
    return act;
}

// ---------------------------------------------------------------------------
// Tests — one per arm with bindable fields
// ---------------------------------------------------------------------------

static void test_value_fields_resolved() {
    printf("--- value fields are resolved per arm ---\n");

    {
        ButtonAction act = make_action(ACTION_TYPE_MQTT);
        strlcpy(act.payload.mqtt.mqtt_topic,   "[mock:t]", sizeof(act.payload.mqtt.mqtt_topic));
        strlcpy(act.payload.mqtt.mqtt_payload, "[mock:p]", sizeof(act.payload.mqtt.mqtt_payload));
        resolve_action_bindings(act);
        check_str(act.payload.mqtt.mqtt_topic,   "RESOLVED", "mqtt_topic resolved");
        check_str(act.payload.mqtt.mqtt_payload, "RESOLVED", "mqtt_payload resolved");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_KEY);
        strlcpy(act.payload.key.key_sequence, "[mock:k]", sizeof(act.payload.key.key_sequence));
        resolve_action_bindings(act);
        check_str(act.payload.key.key_sequence, "RESOLVED", "key_sequence resolved");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_BEEP);
        strlcpy(act.payload.beep.beep_pattern, "[mock:b]", sizeof(act.payload.beep.beep_pattern));
        resolve_action_bindings(act);
        check_str(act.payload.beep.beep_pattern, "RESOLVED", "beep_pattern resolved");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_VOLUME);
        strlcpy(act.payload.volume.volume_value, "[mock:v]", sizeof(act.payload.volume.volume_value));
        resolve_action_bindings(act);
        check_str(act.payload.volume.volume_value, "RESOLVED", "volume_value resolved");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_BRIGHTNESS);
        strlcpy(act.payload.brightness.brightness_value, "[mock:br]", sizeof(act.payload.brightness.brightness_value));
        resolve_action_bindings(act);
        check_str(act.payload.brightness.brightness_value, "RESOLVED", "brightness_value resolved");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_TIMER);
        strlcpy(act.payload.timer.timer_value, "[mock:tv]", sizeof(act.payload.timer.timer_value));
        resolve_action_bindings(act);
        check_str(act.payload.timer.timer_value, "RESOLVED", "timer_value resolved");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_NOTIFY);
        strlcpy(act.payload.notify.notify_text,         "[mock:nt]", sizeof(act.payload.notify.notify_text));
        strlcpy(act.payload.notify.notify_duration_ms,  "[mock:nd]", sizeof(act.payload.notify.notify_duration_ms));
        strlcpy(act.payload.notify.notify_text_color,   "[mock:tc]", sizeof(act.payload.notify.notify_text_color));
        strlcpy(act.payload.notify.notify_bg_color,     "[mock:bg]", sizeof(act.payload.notify.notify_bg_color));
        strlcpy(act.payload.notify.notify_border_color, "[mock:bc]", sizeof(act.payload.notify.notify_border_color));
        resolve_action_bindings(act);
        check_str(act.payload.notify.notify_text,         "RESOLVED", "notify_text resolved");
        check_str(act.payload.notify.notify_duration_ms,  "RESOLVED", "notify_duration_ms resolved");
        check_str(act.payload.notify.notify_text_color,   "RESOLVED", "notify_text_color resolved");
        check_str(act.payload.notify.notify_bg_color,     "RESOLVED", "notify_bg_color resolved");
        check_str(act.payload.notify.notify_border_color, "RESOLVED", "notify_border_color resolved");
    }
}

static void test_structural_fields_excluded() {
    printf("--- structural fields are NOT resolved ---\n");

    {
        // screen_id IS resolved in production (production code listed it in
        // the resolvable set), so this test documents that behavior.
        ButtonAction act = make_action(ACTION_TYPE_SCREEN);
        strlcpy(act.payload.screen.screen_id, "[mock:x]", sizeof(act.payload.screen.screen_id));
        resolve_action_bindings(act);
        check_str(act.payload.screen.screen_id, "RESOLVED", "screen_id is resolved (matches prod)");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_VOLUME);
        strlcpy(act.payload.volume.volume_mode,  "[m:x]", sizeof(act.payload.volume.volume_mode));
        strlcpy(act.payload.volume.volume_value, "55",    sizeof(act.payload.volume.volume_value));
        resolve_action_bindings(act);
        check_str(act.payload.volume.volume_mode, "[m:x]", "volume_mode not resolved");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_BRIGHTNESS);
        strlcpy(act.payload.brightness.brightness_mode, "[m:x]", sizeof(act.payload.brightness.brightness_mode));
        resolve_action_bindings(act);
        check_str(act.payload.brightness.brightness_mode, "[m:x]", "brightness_mode not resolved");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_TIMER);
        strlcpy(act.payload.timer.timer_command, "[mock:x]", sizeof(act.payload.timer.timer_command));
        resolve_action_bindings(act);
        check_str(act.payload.timer.timer_command, "[mock:x]", "timer_command not resolved");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_SYSTEM);
        strlcpy(act.payload.system.system_command, "[mock:x]", sizeof(act.payload.system.system_command));
        resolve_action_bindings(act);
        check_str(act.payload.system.system_command, "[mock:x]", "system_command not resolved");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_NOTIFY);
        strlcpy(act.payload.notify.notify_location, "[m:x]", sizeof(act.payload.notify.notify_location));
        resolve_action_bindings(act);
        check_str(act.payload.notify.notify_location, "[m:x]", "notify_location not resolved");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_SOUND);
        strlcpy(act.payload.sound.sound_file, "[mock:x]", sizeof(act.payload.sound.sound_file));
        resolve_action_bindings(act);
        check_str(act.payload.sound.sound_file, "[mock:x]", "sound_file not resolved");
    }
}

static void test_plain_values_unchanged() {
    printf("--- plain values without bindings stay unchanged ---\n");

    {
        ButtonAction act = make_action(ACTION_TYPE_MQTT);
        strlcpy(act.payload.mqtt.mqtt_topic,   "home/light/set", sizeof(act.payload.mqtt.mqtt_topic));
        strlcpy(act.payload.mqtt.mqtt_payload, "ON",             sizeof(act.payload.mqtt.mqtt_payload));
        resolve_action_bindings(act);
        check_str(act.payload.mqtt.mqtt_topic,   "home/light/set", "plain mqtt_topic unchanged");
        check_str(act.payload.mqtt.mqtt_payload, "ON",             "plain mqtt_payload unchanged");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_TIMER);
        strlcpy(act.payload.timer.timer_value, "300", sizeof(act.payload.timer.timer_value));
        resolve_action_bindings(act);
        check_str(act.payload.timer.timer_value, "300", "plain timer_value unchanged");
    }
}

static void test_empty_fields_stay_empty() {
    printf("--- empty fields stay empty ---\n");
    ButtonAction act = make_action(ACTION_TYPE_MQTT);
    resolve_action_bindings(act);
    check_str(act.payload.mqtt.mqtt_topic,   "", "empty mqtt_topic stays empty");
    check_str(act.payload.mqtt.mqtt_payload, "", "empty mqtt_payload stays empty");

    ButtonAction n = make_action(ACTION_TYPE_NOTIFY);
    resolve_action_bindings(n);
    check_str(n.payload.notify.notify_text,        "", "empty notify_text stays empty");
    check_str(n.payload.notify.notify_duration_ms, "", "empty notify_duration_ms stays empty");
}

static void test_curly_brace_step_not_affected() {
    printf("--- {step} placeholder is not affected ---\n");
    {
        ButtonAction act = make_action(ACTION_TYPE_VOLUME);
        strlcpy(act.payload.volume.volume_value, "{step}", sizeof(act.payload.volume.volume_value));
        resolve_action_bindings(act);
        check_str(act.payload.volume.volume_value, "{step}", "{step} in volume_value unchanged");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_BRIGHTNESS);
        strlcpy(act.payload.brightness.brightness_value, "{step}", sizeof(act.payload.brightness.brightness_value));
        resolve_action_bindings(act);
        check_str(act.payload.brightness.brightness_value, "{step}", "{step} in brightness_value unchanged");
    }
    {
        ButtonAction act = make_action(ACTION_TYPE_TIMER);
        strlcpy(act.payload.timer.timer_value, "{step}", sizeof(act.payload.timer.timer_value));
        resolve_action_bindings(act);
        check_str(act.payload.timer.timer_value, "{step}", "{step} in timer_value unchanged");
    }
}

static void test_mixed_static_and_binding() {
    printf("--- mixed static text and binding token ---\n");
    ButtonAction act = make_action(ACTION_TYPE_NOTIFY);
    strlcpy(act.payload.notify.notify_text, "CPU: [echo:42]%", sizeof(act.payload.notify.notify_text));
    resolve_action_bindings(act);
    check_str(act.payload.notify.notify_text, "CPU: 42%", "mixed binding resolved");
}

static void test_small_field_truncation() {
    printf("--- small field truncation on resolve ---\n");
    ButtonAction act = make_action(ACTION_TYPE_VOLUME);
    // volume_value is CONFIG_VALUE_MAX_LEN = 16 bytes
    // [long:x] fits in 16 bytes; resolver returns 19-char string, truncated to 15+nul
    strlcpy(act.payload.volume.volume_value, "[long:x]", sizeof(act.payload.volume.volume_value));
    resolve_action_bindings(act);
    check_true(strlen(act.payload.volume.volume_value) == CONFIG_VALUE_MAX_LEN - 1,
               "truncated to field size");
    check_str(act.payload.volume.volume_value, "123456789012345", "truncated content correct");
}

static void test_unregistered_scheme() {
    printf("--- unregistered scheme produces error marker ---\n");
    ButtonAction act = make_action(ACTION_TYPE_MQTT);
    strlcpy(act.payload.mqtt.mqtt_payload, "[nosuch:key]", sizeof(act.payload.mqtt.mqtt_payload));
    resolve_action_bindings(act);
    check_str(act.payload.mqtt.mqtt_payload, "ERR:unknown", "unregistered scheme produces error");
}

static void test_multiple_bindings_in_one_field() {
    printf("--- multiple binding tokens in one field ---\n");
    ButtonAction act = make_action(ACTION_TYPE_NOTIFY);
    strlcpy(act.payload.notify.notify_text, "[echo:hello] [echo:world]", sizeof(act.payload.notify.notify_text));
    resolve_action_bindings(act);
    check_str(act.payload.notify.notify_text, "hello world", "multiple bindings resolved");
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
