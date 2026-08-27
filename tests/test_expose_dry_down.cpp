// ============================================================================
// Unit tests for expose timer dry-down compensation
// ============================================================================
// Host-native: compiled with controllable millis() mock and stubs for
// relay, binding, audio, and config dependencies.
//
// Block real headers from expose_timer.cpp — we provide our own stubs.
// config_manager.h uses #ifndef guard; relay/web_portal use #pragma once
// so we provide stub headers via -I priority (tests/ searched before src/app/).

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <string>

// relay_controller.h (pulled in transitively by expose_timer.cpp) includes
// <Arduino.h> and declares a String-typed config accessor. The host Arduino.h
// stub has no String, so alias it to std::string for the declarations we never
// call. expose_timer.cpp itself only uses the relay_request()/relay_is_on()
// surface, which is stubbed below.
using String = std::string;

// Pre-define config_manager.h guard to block the real header
#define CONFIG_MANAGER_H
#define CONFIG_BEEP_PATTERN_MAX_LEN 64

// Minimal DeviceConfig stub — only the field used by expose_timer.cpp
struct DeviceConfig { char tap_beep[CONFIG_BEEP_PATTERN_MAX_LEN]; };

// ---------------------------------------------------------------------------
// Controllable millis() mock
// ---------------------------------------------------------------------------
static unsigned long s_millis = 0;
extern "C" unsigned long millis() { return s_millis; }

// ---------------------------------------------------------------------------
// Relay stubs — track relay state for assertions
// ---------------------------------------------------------------------------
static bool s_relay_on = false;
void relay_controller_init() {}
void relay_load_config() {}
void relay_request(bool on) { s_relay_on = on; }
bool relay_is_on() { return s_relay_on; }
void relay_loop() {}
void relay_controller_clear_config() {}
void relay_queue_shelly(const char*, uint8_t, bool) {}

// ---------------------------------------------------------------------------
// Binding template stub — captures the registered resolver for direct calls
// ---------------------------------------------------------------------------
#include "binding_template.h"
static binding_resolver_fn s_expose_resolver = nullptr;

bool binding_template_register(const char* scheme, binding_resolver_fn resolver,
                               binding_topic_collector_fn collector,
                               const BindingSchemeSpec& spec) {
    (void)scheme;
    (void)collector;
    (void)spec;
    s_expose_resolver = resolver;
    return true;
}

// ---------------------------------------------------------------------------
// Config / audio stubs
// ---------------------------------------------------------------------------
static DeviceConfig s_config = { .tap_beep = "" };
DeviceConfig* web_portal_get_current_config() { return &s_config; }

#ifdef HAS_AUDIO
void audio_beep(const char*, int) {}
#endif

// ---------------------------------------------------------------------------
// Meter getter stubs — return neutral values
// ---------------------------------------------------------------------------
float meter_get_lref() { return 0.0f; }
float meter_get_zone5_time() { return 0.0f; }
float meter_get_bright() { return -1.0f; }
float meter_get_dark() { return -1.0f; }
float meter_get_sbr() { return 0.0f; }
float meter_get_grade() { return 0.0f; }
const char* meter_get_grade_label() { return nullptr; }
float meter_get_mag_factor() { return 0.0f; }

// ---------------------------------------------------------------------------
// Print log stub
// ---------------------------------------------------------------------------
#include "print_log.h"
void print_log_pend_exposure(const PrintLogExposureData&) {}
void print_log_clear_id() {}

// ---------------------------------------------------------------------------
// Unit under test — include the .cpp directly (project test pattern)
// ---------------------------------------------------------------------------
#include "expose_timer.h"
#include "expose_timer.cpp"

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------
static int g_tests = 0;
static int g_passed = 0;

#define TEST(name) static void name()
#define RUN(name) do {                                       \
    g_tests++;                                               \
    printf("  %-55s ", #name);                               \
    name();                                                  \
    g_passed++;                                              \
    printf("PASS\n");                                        \
} while (0)

#define ASSERT_FLOAT_EQ(actual, expected) do {               \
    float _a = (actual), _e = (expected);                    \
    if (fabsf(_a - _e) > 0.01f) {                           \
        printf("FAIL\n    %s:%d: %.3f != %.3f\n",           \
               __FILE__, __LINE__, _a, _e);                  \
        assert(false);                                       \
    }                                                        \
} while (0)

#define ASSERT_STR_EQ(actual, expected) do {                 \
    if (strcmp((actual), (expected)) != 0) {                  \
        printf("FAIL\n    %s:%d: \"%s\" != \"%s\"\n",       \
               __FILE__, __LINE__, (actual), (expected));    \
        assert(false);                                       \
    }                                                        \
} while (0)

#define ASSERT_TRUE(cond) do {                               \
    if (!(cond)) {                                           \
        printf("FAIL\n    %s:%d: assertion failed\n",        \
               __FILE__, __LINE__);                          \
        assert(false);                                       \
    }                                                        \
} while (0)

// ---------------------------------------------------------------------------
// Helper — reset timer to clean state between tests
// ---------------------------------------------------------------------------
static void reset_timer() {
    s_millis = 0;
    s_relay_on = false;
    // Stop timer and reset state
    expose_timer_dispatch("stop", "");
    expose_timer_dispatch("set_time", "10.0");
    expose_timer_dispatch("set_dry_down", "0.0");
}

// ---------------------------------------------------------------------------
// Helper — resolve a binding key and return the output string
// ---------------------------------------------------------------------------
static char s_resolve_buf[128];
static const char* resolve(const char* params) {
    assert(s_expose_resolver);
    s_resolve_buf[0] = '\0';
    s_expose_resolver(params, s_resolve_buf, sizeof(s_resolve_buf));
    return s_resolve_buf;
}

// ============================================================================
// Tests: AC1 — dry_down default is 0.0
// ============================================================================

TEST(dry_down_default_is_zero) {
    reset_timer();
    ASSERT_STR_EQ(resolve("dry_down"), "0.0");
}

// ============================================================================
// Tests: AC2 — set_dry_down clamps to 0–15
// ============================================================================

TEST(set_dry_down_basic) {
    reset_timer();
    expose_timer_dispatch("set_dry_down", "8.0");
    ASSERT_STR_EQ(resolve("dry_down"), "8.0");
}

TEST(set_dry_down_clamps_low) {
    reset_timer();
    expose_timer_dispatch("set_dry_down", "-5.0");
    ASSERT_STR_EQ(resolve("dry_down"), "0.0");
}

TEST(set_dry_down_clamps_high) {
    reset_timer();
    expose_timer_dispatch("set_dry_down", "25.0");
    ASSERT_STR_EQ(resolve("dry_down"), "15.0");
}

TEST(set_dry_down_snaps_tenth) {
    reset_timer();
    expose_timer_dispatch("set_dry_down", "7.35");
    ASSERT_STR_EQ(resolve("dry_down"), "7.4");  // snap_tenth rounds
}

// ============================================================================
// Tests: AC3 — adjust_dry_down
// ============================================================================

TEST(adjust_dry_down_positive) {
    reset_timer();
    expose_timer_dispatch("set_dry_down", "5.0");
    expose_timer_dispatch("adjust_dry_down", "2.5");
    ASSERT_STR_EQ(resolve("dry_down"), "7.5");
}

TEST(adjust_dry_down_negative) {
    reset_timer();
    expose_timer_dispatch("set_dry_down", "5.0");
    expose_timer_dispatch("adjust_dry_down", "-3.0");
    ASSERT_STR_EQ(resolve("dry_down"), "2.0");
}

TEST(adjust_dry_down_clamps_at_zero) {
    reset_timer();
    expose_timer_dispatch("set_dry_down", "2.0");
    expose_timer_dispatch("adjust_dry_down", "-10.0");
    ASSERT_STR_EQ(resolve("dry_down"), "0.0");
}

TEST(adjust_dry_down_clamps_at_max) {
    reset_timer();
    expose_timer_dispatch("set_dry_down", "14.0");
    expose_timer_dispatch("adjust_dry_down", "5.0");
    ASSERT_STR_EQ(resolve("dry_down"), "15.0");
}

// ============================================================================
// Tests: AC4 — rejected while running/paused
// ============================================================================

TEST(set_dry_down_rejected_while_running) {
    reset_timer();
    expose_timer_dispatch("set_dry_down", "5.0");
    expose_timer_dispatch("start", "");
    expose_timer_dispatch("set_dry_down", "10.0");
    ASSERT_STR_EQ(resolve("dry_down"), "5.0");  // unchanged
    expose_timer_dispatch("stop", "");
}

TEST(set_dry_down_rejected_while_paused) {
    reset_timer();
    expose_timer_dispatch("set_dry_down", "5.0");
    expose_timer_dispatch("start", "");
    expose_timer_dispatch("pause", "");
    expose_timer_dispatch("set_dry_down", "10.0");
    ASSERT_STR_EQ(resolve("dry_down"), "5.0");  // unchanged
    expose_timer_dispatch("stop", "");
}

TEST(adjust_dry_down_rejected_while_running) {
    reset_timer();
    expose_timer_dispatch("set_dry_down", "5.0");
    expose_timer_dispatch("start", "");
    expose_timer_dispatch("adjust_dry_down", "3.0");
    ASSERT_STR_EQ(resolve("dry_down"), "5.0");  // unchanged
    expose_timer_dispatch("stop", "");
}

TEST(set_dry_down_allowed_in_focus) {
    reset_timer();
    expose_timer_dispatch("focus", "");
    expose_timer_dispatch("set_dry_down", "8.0");
    ASSERT_STR_EQ(resolve("dry_down"), "8.0");  // accepted
    expose_timer_dispatch("stop", "");
}

// ============================================================================
// Tests: AC5 — [expose:dry_down] binding
// ============================================================================

TEST(dry_down_binding_format) {
    reset_timer();
    expose_timer_dispatch("set_dry_down", "8.0");
    ASSERT_STR_EQ(resolve("dry_down"), "8.0");
}

// ============================================================================
// Tests: AC6 — [expose:effective_time] computation
// ============================================================================

TEST(effective_time_no_dry_down) {
    reset_timer();
    expose_timer_dispatch("set_time", "10.0");
    ASSERT_STR_EQ(resolve("effective_time"), "10.0");
}

TEST(effective_time_with_dry_down) {
    reset_timer();
    expose_timer_dispatch("set_time", "10.0");
    expose_timer_dispatch("set_dry_down", "8.0");
    // 10.0 * (1 - 8/100) = 10.0 * 0.92 = 9.2
    ASSERT_STR_EQ(resolve("effective_time"), "9.2");
}

TEST(effective_time_10_percent) {
    reset_timer();
    expose_timer_dispatch("set_time", "10.0");
    expose_timer_dispatch("set_dry_down", "10.0");
    // 10.0 * 0.90 = 9.0
    ASSERT_STR_EQ(resolve("effective_time"), "9.0");
}

TEST(effective_time_15_percent_max) {
    reset_timer();
    expose_timer_dispatch("set_time", "20.0");
    expose_timer_dispatch("set_dry_down", "15.0");
    // 20.0 * 0.85 = 17.0
    ASSERT_STR_EQ(resolve("effective_time"), "17.0");
}

// ============================================================================
// Tests: AC7 — effective_time with format suffix
// ============================================================================

TEST(effective_time_formatted) {
    reset_timer();
    expose_timer_dispatch("set_time", "10.0");
    expose_timer_dispatch("set_dry_down", "8.0");
    // effective = 9.2s → mm:ss.d = 0:09.2
    ASSERT_STR_EQ(resolve("effective_time;mm:ss.d"), "0:09.2");
}

TEST(effective_time_formatted_ss) {
    reset_timer();
    expose_timer_dispatch("set_time", "10.0");
    expose_timer_dispatch("set_dry_down", "8.0");
    // effective = 9.2s → ss = 9
    ASSERT_STR_EQ(resolve("effective_time;ss"), "9");
}

// ============================================================================
// Tests: [expose:time] is NOT affected by dry-down (regression)
// ============================================================================

TEST(time_binding_unaffected_by_dry_down) {
    reset_timer();
    expose_timer_dispatch("set_time", "10.0");
    expose_timer_dispatch("set_dry_down", "8.0");
    // [expose:time] must still show the set time, not effective time
    ASSERT_STR_EQ(resolve("time"), "10.0");
}

TEST(time_binding_formatted_unaffected_by_dry_down) {
    reset_timer();
    expose_timer_dispatch("set_time", "10.0");
    expose_timer_dispatch("set_dry_down", "8.0");
    // [expose:time;ss.d] must show "10.0", not "9.2"
    ASSERT_STR_EQ(resolve("time;ss.d"), "10.0");
}

// ============================================================================
// Tests: AC8 — countdown uses effective_time
// ============================================================================

TEST(countdown_uses_effective_time) {
    reset_timer();
    expose_timer_dispatch("set_time", "10.0");
    expose_timer_dispatch("set_dry_down", "8.0");
    // effective = 9.2s = 9200ms

    expose_timer_dispatch("start", "");
    s_millis = 5000;  // 5s elapsed

    // remaining should be 9200 - 5000 = 4200ms = 4.2s
    ASSERT_STR_EQ(resolve("remaining"), "4.2");

    expose_timer_dispatch("stop", "");
}

// ============================================================================
// Tests: AC10 — zero fast-path
// ============================================================================

TEST(zero_dry_down_exact_equivalence) {
    reset_timer();
    expose_timer_dispatch("set_time", "8.5");
    // With 0% dry-down, effective_time == set_time exactly
    ASSERT_STR_EQ(resolve("effective_time"), "8.5");
    ASSERT_STR_EQ(resolve("time"), "8.5");
}

// ============================================================================
// Tests: AC11 — adjust_stops modifies set_time, not effective_time
// ============================================================================

TEST(adjust_stops_modifies_set_time) {
    reset_timer();
    expose_timer_dispatch("set_time", "8.0");
    expose_timer_dispatch("set_dry_down", "10.0");
    expose_timer_dispatch("adjust_stops", "1.0");
    // set_time should be 16.0 (8 * 2^1)
    ASSERT_STR_EQ(resolve("time"), "16.0");
    // effective_time should be 16.0 * 0.90 = 14.4
    ASSERT_STR_EQ(resolve("effective_time"), "14.4");
}

TEST(adjust_seconds_modifies_set_time) {
    reset_timer();
    expose_timer_dispatch("set_time", "10.0");
    expose_timer_dispatch("set_dry_down", "10.0");
    expose_timer_dispatch("adjust_seconds", "2.0");
    // set_time = 12.0
    ASSERT_STR_EQ(resolve("time"), "12.0");
    // effective = 12.0 * 0.90 = 10.8
    ASSERT_STR_EQ(resolve("effective_time"), "10.8");
}

// ============================================================================
// Tests: AC14 — zero exposure_time with dry_down
// ============================================================================

TEST(zero_time_with_dry_down) {
    reset_timer();
    expose_timer_dispatch("set_time", "0.0");
    expose_timer_dispatch("set_dry_down", "8.0");
    ASSERT_STR_EQ(resolve("effective_time"), "0.0");
}

// ============================================================================
// Tests: AC12/13 — edge cases
// ============================================================================

TEST(set_dry_down_empty_value_noop) {
    reset_timer();
    expose_timer_dispatch("set_dry_down", "5.0");
    expose_timer_dispatch("set_dry_down", "");
    // strtof("", NULL) returns 0.0 — sets to 0.0 (clamped minimum)
    ASSERT_STR_EQ(resolve("dry_down"), "0.0");
}

// ============================================================================
// Tests: timer expiry uses effective time
// ============================================================================

TEST(timer_expires_at_effective_time) {
    reset_timer();
    expose_timer_dispatch("set_time", "10.0");
    expose_timer_dispatch("set_dry_down", "10.0");
    // effective = 9.0s = 9000ms

    expose_timer_dispatch("start", "");
    ASSERT_TRUE(s_relay_on);

    // At 8999ms — should still be running
    s_millis = 8999;
    expose_timer_tick();
    ASSERT_STR_EQ(resolve("state"), "running");
    ASSERT_TRUE(s_relay_on);

    // At 9000ms — should expire
    s_millis = 9000;
    expose_timer_tick();
    ASSERT_STR_EQ(resolve("state"), "stopped");
    ASSERT_TRUE(!s_relay_on);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    // Initialize — registers the binding resolver
    expose_timer_init();

    printf("=== Expose Timer: Dry-Down Compensation Tests ===\n");

    // AC1: default
    RUN(dry_down_default_is_zero);

    // AC2: set_dry_down
    RUN(set_dry_down_basic);
    RUN(set_dry_down_clamps_low);
    RUN(set_dry_down_clamps_high);
    RUN(set_dry_down_snaps_tenth);

    // AC3: adjust_dry_down
    RUN(adjust_dry_down_positive);
    RUN(adjust_dry_down_negative);
    RUN(adjust_dry_down_clamps_at_zero);
    RUN(adjust_dry_down_clamps_at_max);

    // AC4: rejected while active
    RUN(set_dry_down_rejected_while_running);
    RUN(set_dry_down_rejected_while_paused);
    RUN(adjust_dry_down_rejected_while_running);
    RUN(set_dry_down_allowed_in_focus);

    // AC5: binding
    RUN(dry_down_binding_format);

    // AC6: effective_time
    RUN(effective_time_no_dry_down);
    RUN(effective_time_with_dry_down);
    RUN(effective_time_10_percent);
    RUN(effective_time_15_percent_max);

    // AC7: formatted effective_time
    RUN(effective_time_formatted);
    RUN(effective_time_formatted_ss);

    // AC8: countdown
    RUN(countdown_uses_effective_time);

    // AC10: zero fast-path
    RUN(zero_dry_down_exact_equivalence);

    // AC11: adjust_stops/seconds
    RUN(adjust_stops_modifies_set_time);
    RUN(adjust_seconds_modifies_set_time);

    // AC14: zero time edge case
    RUN(zero_time_with_dry_down);

    // AC12/13: edge cases
    RUN(set_dry_down_empty_value_noop);

    // Regression: [expose:time] must not be affected by dry-down
    RUN(time_binding_unaffected_by_dry_down);
    RUN(time_binding_formatted_unaffected_by_dry_down);

    // Timer expiry
    RUN(timer_expires_at_effective_time);

    printf("\n%d/%d tests passed\n", g_passed, g_tests);
    return (g_passed == g_tests) ? 0 : 1;
}
