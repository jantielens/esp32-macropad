// ============================================================================
// Unit tests for meter magnification compensation
// ============================================================================
// Host-native: compiled with stubs for sensor, expose_timer, binding, and
// FreeRTOS dependencies. Validates mag binding resolution, generation counter
// invalidation, and reject-if-busy command behavior.
//
// Block real headers from meter.cpp — we provide our own stubs.

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// portMUX stubs — no-op on host (single-threaded tests)
// ---------------------------------------------------------------------------
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x)  ((void)(x))

// ---------------------------------------------------------------------------
// TSL2591 sensor stub — captures whether a read was requested
// ---------------------------------------------------------------------------
static float s_sensor_lux = 100.0f;
bool tsl2591_init() { return true; }
float tsl2591_read_lux() { return s_sensor_lux; }
bool tsl2591_is_connected() { return true; }

// We need to shadow the real header so meter.cpp doesn't try to include it
#define TSL2591_SENSOR_H

// ---------------------------------------------------------------------------
// Expose timer stub — controllable get_time return
// ---------------------------------------------------------------------------
static float s_expose_time = 0.0f;
float expose_timer_get_time() { return s_expose_time; }
void expose_timer_set_time(float) {}

// Shadow the real header
#define EXPOSE_TIMER_H

// ---------------------------------------------------------------------------
// Binding template stub — captures the registered resolver
// ---------------------------------------------------------------------------
#include "binding_template.h"
static binding_resolver_fn s_meter_resolver = nullptr;

bool binding_template_register(const char* scheme, binding_resolver_fn resolver,
                               binding_topic_collector_fn collector) {
    (void)scheme;
    (void)collector;
    s_meter_resolver = resolver;
    return true;
}

// ---------------------------------------------------------------------------
// millis() stub (required by Arduino.h)
// ---------------------------------------------------------------------------
extern "C" unsigned long millis() { return 0; }

// ---------------------------------------------------------------------------
// Unit under test — include the .cpp directly (project test pattern)
// ---------------------------------------------------------------------------
#include "meter.h"
#include "meter.cpp"

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

#define ASSERT_FALSE(cond) do {                              \
    if ((cond)) {                                            \
        printf("FAIL\n    %s:%d: expected false\n",          \
               __FILE__, __LINE__);                          \
        assert(false);                                       \
    }                                                        \
} while (0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static char s_resolve_buf[128];

static const char* resolve(const char* params) {
    assert(s_meter_resolver);
    s_resolve_buf[0] = '\0';
    s_meter_resolver(params, s_resolve_buf, sizeof(s_resolve_buf));
    return s_resolve_buf;
}

// Reset meter state to defaults between tests
static void reset_meter() {
    g_meter.lref        = 0.0f;
    g_meter.zone5_time  = 0.0f;
    g_meter.l_bright    = -1.0f;
    g_meter.l_dark      = -1.0f;
    g_meter.sbr         = 0.0f;
    g_meter.grade       = 0.0f;
    g_meter.grade_label = "---";
    g_meter.time_s      = -1.0f;
    g_meter.has_results = false;
    g_meter.mag_lux_a   = -1.0f;
    g_meter.mag_lux_b   = -1.0f;
    g_meter.mag_generation = 0;
    g_meter.mag_a_gen   = 0;
    g_meter.mag_b_gen   = 0;
    s_pending_callback = nullptr;
    s_sensor_lux  = 100.0f;
    s_expose_time = 0.0f;
}

// Simulate a full sensor read cycle: dispatches the pending callback via meter_loop
static void complete_pending_read() {
    meter_loop();
}

// ============================================================================
// Tests: mag_lux_a / mag_lux_b bindings — default "---"
// ============================================================================

TEST(mag_lux_a_default_is_dash) {
    reset_meter();
    ASSERT_STR_EQ(resolve("mag_lux_a"), "---");
}

TEST(mag_lux_b_default_is_dash) {
    reset_meter();
    ASSERT_STR_EQ(resolve("mag_lux_b"), "---");
}

// ============================================================================
// Tests: mag_measure_a / mag_measure_b populate lux values
// ============================================================================

TEST(mag_measure_a_populates_lux) {
    reset_meter();
    s_sensor_lux = 250.5f;
    meter_dispatch("mag_measure_a", "");
    complete_pending_read();
    ASSERT_STR_EQ(resolve("mag_lux_a"), "250.5000");
}

TEST(mag_measure_b_populates_lux) {
    reset_meter();
    s_sensor_lux = 50.25f;
    meter_dispatch("mag_measure_b", "");
    complete_pending_read();
    ASSERT_STR_EQ(resolve("mag_lux_b"), "50.2500");
}

// ============================================================================
// Tests: mag_factor binding
// ============================================================================

TEST(mag_factor_default_is_dash) {
    reset_meter();
    ASSERT_STR_EQ(resolve("mag_factor"), "---");
}

TEST(mag_factor_with_only_a_is_dash) {
    reset_meter();
    g_meter.mag_lux_a = 200.0f;
    ASSERT_STR_EQ(resolve("mag_factor"), "---");
}

TEST(mag_factor_with_only_b_is_dash) {
    reset_meter();
    g_meter.mag_lux_b = 100.0f;
    ASSERT_STR_EQ(resolve("mag_factor"), "---");
}

TEST(mag_factor_computed_correctly) {
    reset_meter();
    g_meter.mag_lux_a = 200.0f;
    g_meter.mag_lux_b = 100.0f;
    ASSERT_STR_EQ(resolve("mag_factor"), "2.0");
}

TEST(mag_factor_fractional) {
    reset_meter();
    g_meter.mag_lux_a = 150.0f;
    g_meter.mag_lux_b = 100.0f;
    ASSERT_STR_EQ(resolve("mag_factor"), "1.5");
}

TEST(mag_factor_b_below_min_valid_lux_is_dash) {
    reset_meter();
    g_meter.mag_lux_a = 200.0f;
    g_meter.mag_lux_b = 0.0005f;  // below MIN_VALID_LUX (0.001)
    ASSERT_STR_EQ(resolve("mag_factor"), "---");
}

// ============================================================================
// Tests: mag_time binding
// ============================================================================

TEST(mag_time_default_is_dash) {
    reset_meter();
    ASSERT_STR_EQ(resolve("mag_time"), "---");
}

TEST(mag_time_no_expose_time_is_dash) {
    reset_meter();
    g_meter.mag_lux_a = 200.0f;
    g_meter.mag_lux_b = 100.0f;
    s_expose_time = 0.0f;
    ASSERT_STR_EQ(resolve("mag_time"), "---");
}

TEST(mag_time_computed_correctly) {
    reset_meter();
    g_meter.mag_lux_a = 200.0f;
    g_meter.mag_lux_b = 100.0f;
    s_expose_time = 10.0f;
    // 10.0 * (200/100) = 20.0
    ASSERT_STR_EQ(resolve("mag_time"), "20.0");
}

TEST(mag_time_fractional_result) {
    reset_meter();
    g_meter.mag_lux_a = 150.0f;
    g_meter.mag_lux_b = 100.0f;
    s_expose_time = 8.0f;
    // 8.0 * (150/100) = 12.0
    ASSERT_STR_EQ(resolve("mag_time"), "12.0");
}

TEST(mag_time_rounds_to_tenth) {
    reset_meter();
    g_meter.mag_lux_a = 333.0f;
    g_meter.mag_lux_b = 100.0f;
    s_expose_time = 10.0f;
    // 10.0 * (333/100) = 33.3
    ASSERT_STR_EQ(resolve("mag_time"), "33.3");
}

TEST(mag_time_b_below_min_valid_is_dash) {
    reset_meter();
    g_meter.mag_lux_a = 200.0f;
    g_meter.mag_lux_b = 0.0001f;
    s_expose_time = 10.0f;
    ASSERT_STR_EQ(resolve("mag_time"), "---");
}

// ============================================================================
// Tests: mag_clear resets both readings
// ============================================================================

TEST(mag_clear_resets_readings) {
    reset_meter();
    g_meter.mag_lux_a = 200.0f;
    g_meter.mag_lux_b = 100.0f;
    meter_dispatch("mag_clear", "");
    ASSERT_STR_EQ(resolve("mag_lux_a"), "---");
    ASSERT_STR_EQ(resolve("mag_lux_b"), "---");
    ASSERT_STR_EQ(resolve("mag_factor"), "---");
}

// ============================================================================
// Tests: generation counter — mag_clear invalidates in-flight reads
// ============================================================================

TEST(mag_clear_invalidates_inflight_a) {
    reset_meter();
    s_sensor_lux = 200.0f;
    meter_dispatch("mag_measure_a", "");
    // Before the sensor read completes, user clears
    meter_dispatch("mag_clear", "");
    // Now the sensor read completes — should be discarded
    complete_pending_read();
    ASSERT_STR_EQ(resolve("mag_lux_a"), "---");
}

TEST(mag_clear_invalidates_inflight_b) {
    reset_meter();
    s_sensor_lux = 100.0f;
    meter_dispatch("mag_measure_b", "");
    meter_dispatch("mag_clear", "");
    complete_pending_read();
    ASSERT_STR_EQ(resolve("mag_lux_b"), "---");
}

TEST(mag_measure_after_clear_uses_new_generation) {
    reset_meter();
    // First measure
    s_sensor_lux = 200.0f;
    meter_dispatch("mag_measure_a", "");
    complete_pending_read();
    ASSERT_STR_EQ(resolve("mag_lux_a"), "200.0000");

    // Clear, then re-measure with different lux
    meter_dispatch("mag_clear", "");
    ASSERT_STR_EQ(resolve("mag_lux_a"), "---");

    s_sensor_lux = 300.0f;
    meter_dispatch("mag_measure_a", "");
    complete_pending_read();
    ASSERT_STR_EQ(resolve("mag_lux_a"), "300.0000");
}

// ============================================================================
// Tests: reject-if-busy — only one read at a time
// ============================================================================

TEST(second_read_rejected_while_busy) {
    reset_meter();
    s_sensor_lux = 200.0f;
    meter_dispatch("mag_measure_a", "");
    // First read is pending — second should be rejected
    ASSERT_FALSE(meter_request_read([](float) {}));
    // Complete the first to unblock
    complete_pending_read();
}

TEST(read_unblocked_after_completion) {
    reset_meter();
    s_sensor_lux = 200.0f;
    meter_dispatch("mag_measure_a", "");
    complete_pending_read();
    // Now another read should succeed
    s_sensor_lux = 100.0f;
    meter_dispatch("mag_measure_b", "");
    complete_pending_read();
    ASSERT_STR_EQ(resolve("mag_lux_b"), "100.0000");
}

// ============================================================================
// Tests: existing SBR bindings unaffected by mag changes
// ============================================================================

TEST(sbr_bindings_unaffected_by_mag) {
    reset_meter();
    g_meter.lref = 1000.0f;
    g_meter.zone5_time = 10.0f;
    g_meter.l_bright = 500.0f;
    g_meter.l_dark = 50.0f;
    recompute();
    // Verify SBR computed
    ASSERT_TRUE(g_meter.has_results);
    // Mag values should still be ---
    ASSERT_STR_EQ(resolve("mag_lux_a"), "---");
    ASSERT_STR_EQ(resolve("mag_factor"), "---");
}

// ============================================================================
// Tests: bad key returns error
// ============================================================================

TEST(bad_key_returns_error) {
    reset_meter();
    ASSERT_STR_EQ(resolve("nonexistent"), "ERR:bad_key");
}

TEST(empty_key_returns_error) {
    reset_meter();
    ASSERT_STR_EQ(resolve(""), "ERR:no_key");
}

// ============================================================================
// main
// ============================================================================

int main() {
    printf("=== Meter magnification compensation tests ===\n");
    meter_init();

    // mag_lux bindings
    RUN(mag_lux_a_default_is_dash);
    RUN(mag_lux_b_default_is_dash);
    RUN(mag_measure_a_populates_lux);
    RUN(mag_measure_b_populates_lux);

    // mag_factor binding
    RUN(mag_factor_default_is_dash);
    RUN(mag_factor_with_only_a_is_dash);
    RUN(mag_factor_with_only_b_is_dash);
    RUN(mag_factor_computed_correctly);
    RUN(mag_factor_fractional);
    RUN(mag_factor_b_below_min_valid_lux_is_dash);

    // mag_time binding
    RUN(mag_time_default_is_dash);
    RUN(mag_time_no_expose_time_is_dash);
    RUN(mag_time_computed_correctly);
    RUN(mag_time_fractional_result);
    RUN(mag_time_rounds_to_tenth);
    RUN(mag_time_b_below_min_valid_is_dash);

    // mag_clear
    RUN(mag_clear_resets_readings);

    // Generation counter invalidation
    RUN(mag_clear_invalidates_inflight_a);
    RUN(mag_clear_invalidates_inflight_b);
    RUN(mag_measure_after_clear_uses_new_generation);

    // Reject-if-busy
    RUN(second_read_rejected_while_busy);
    RUN(read_unblocked_after_completion);

    // Regression: SBR unaffected
    RUN(sbr_bindings_unaffected_by_mag);

    // Error keys
    RUN(bad_key_returns_error);
    RUN(empty_key_returns_error);

    printf("\n%d/%d tests passed.\n", g_passed, g_tests);
    return (g_passed == g_tests) ? 0 : 1;
}
