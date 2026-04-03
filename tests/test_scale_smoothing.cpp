// ============================================================================
// Unit tests for scale_smoothing — EMA, dead-band, flow rate, presets
// ============================================================================
// No Arduino, no ESP32. Compiles and runs on any host.
//
// Build:
//   g++ -std=c++17 -I tests -I src/app
//       tests/test_scale_smoothing.cpp src/app/sensors/scale_smoothing.cpp
//       -o tests/bin/test_scale_smoothing -lm
// Run:
//   ./tests/bin/test_scale_smoothing

#include "../src/app/sensors/scale_smoothing.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT_TRUE(cond, label) do { \
    if (!(cond)) { \
        printf("  %-55s FAIL\n", label); \
        printf("    expected true, got false\n"); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_NEAR(actual, expected, tol, label) do { \
    float _a = (actual), _e = (expected), _t = (tol); \
    if (fabsf(_a - _e) > _t) { \
        printf("  %-55s FAIL\n", label); \
        printf("    expected %.6f ± %.6f, got %.6f\n", _e, _t, _a); \
        g_fail++; return; \
    } \
} while(0)

#define PASS(label) do { \
    printf("  %-55s PASS\n", label); \
    g_pass++; \
} while(0)

// Helper: feed N identical samples at 12.5ms intervals (80 Hz)
static void feed_constant(ScaleSmoothingState &state, float value, int count, uint32_t &ms) {
    for (int i = 0; i < count; i++) {
        ms += 13;  // ~80 Hz
        scale_smoothing_process(state, value, ms);
    }
}

// ============================================================================
// Preset management
// ============================================================================

static void test_preset_get_params() {
    const auto &stable = scale_smoothing_get_params(SCALE_PRESET_STABLE);
    const auto &balanced = scale_smoothing_get_params(SCALE_PRESET_BALANCED);
    const auto &responsive = scale_smoothing_get_params(SCALE_PRESET_RESPONSIVE);

    ASSERT_TRUE(stable.ema_alpha < balanced.ema_alpha, "preset_get_params");
    ASSERT_TRUE(balanced.ema_alpha < responsive.ema_alpha, "preset_get_params");
    ASSERT_TRUE(stable.flow_window_ms > balanced.flow_window_ms, "preset_get_params");
    ASSERT_TRUE(balanced.flow_window_ms > responsive.flow_window_ms, "preset_get_params");
    PASS("preset_get_params");
}

static void test_preset_out_of_range_clamps() {
    const auto &p = scale_smoothing_get_params(99);
    const auto &balanced = scale_smoothing_get_params(SCALE_PRESET_BALANCED);
    ASSERT_NEAR(p.ema_alpha, balanced.ema_alpha, 0.001f, "preset_out_of_range_clamps");
    PASS("preset_out_of_range_clamps");
}

static void test_preset_apply_changes_active() {
    scale_smoothing_apply(SCALE_PRESET_STABLE);
    const auto &a1 = scale_smoothing_active_params();
    const auto &stable = scale_smoothing_get_params(SCALE_PRESET_STABLE);
    ASSERT_NEAR(a1.ema_alpha, stable.ema_alpha, 0.001f, "preset_apply_changes_active");

    scale_smoothing_apply(SCALE_PRESET_RESPONSIVE);
    const auto &a2 = scale_smoothing_active_params();
    const auto &responsive = scale_smoothing_get_params(SCALE_PRESET_RESPONSIVE);
    ASSERT_NEAR(a2.ema_alpha, responsive.ema_alpha, 0.001f, "preset_apply_changes_active");

    // Restore default
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    PASS("preset_apply_changes_active");
}

static void test_preset_apply_out_of_range() {
    scale_smoothing_apply(255);
    const auto &a = scale_smoothing_active_params();
    const auto &balanced = scale_smoothing_get_params(SCALE_PRESET_BALANCED);
    ASSERT_NEAR(a.ema_alpha, balanced.ema_alpha, 0.001f, "preset_apply_out_of_range");
    PASS("preset_apply_out_of_range");
}

// ============================================================================
// EMA priming
// ============================================================================

static void test_ema_first_sample_seeds() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    scale_smoothing_process(state, 100.0f, 1000);

    ASSERT_TRUE(state.ema_primed, "ema_first_sample_seeds");
    ASSERT_NEAR(state.weight_ema, 100.0f, 0.001f, "ema_first_sample_seeds");
    ASSERT_NEAR(state.weight_display, 100.0f, 0.001f, "ema_first_sample_seeds");
    PASS("ema_first_sample_seeds");
}

static void test_ema_second_sample_applies_filter() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    scale_smoothing_process(state, 100.0f, 1000);
    scale_smoothing_process(state, 101.0f, 1013);

    // EMA should be between 100 and 101
    ASSERT_TRUE(state.weight_ema > 100.0f, "ema_second_sample_applies_filter");
    ASSERT_TRUE(state.weight_ema < 101.0f, "ema_second_sample_applies_filter");
    PASS("ema_second_sample_applies_filter");
}

// ============================================================================
// EMA convergence
// ============================================================================

static void test_ema_converges_to_constant() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    // Feed 200 samples of 50.0g — should converge
    feed_constant(state, 50.0f, 200, ms);

    ASSERT_NEAR(state.weight_ema, 50.0f, 0.01f, "ema_converges_to_constant");
    PASS("ema_converges_to_constant");
}

static void test_ema_stable_slower_than_responsive() {
    ScaleSmoothingState state_s = {}, state_r = {};
    uint32_t ms_s = 0, ms_r = 0;

    // Prime both at 0g with their respective presets
    scale_smoothing_apply(SCALE_PRESET_STABLE);
    scale_smoothing_process(state_s, 0.0f, 0);
    ms_s = 0;

    scale_smoothing_apply(SCALE_PRESET_RESPONSIVE);
    scale_smoothing_process(state_r, 0.0f, 0);
    ms_r = 0;

    // Feed 10 samples of 4g to Stable
    scale_smoothing_apply(SCALE_PRESET_STABLE);
    feed_constant(state_s, 4.0f, 10, ms_s);
    float ema_s = state_s.weight_ema;

    // Feed 10 samples of 4g to Responsive
    scale_smoothing_apply(SCALE_PRESET_RESPONSIVE);
    feed_constant(state_r, 4.0f, 10, ms_r);
    float ema_r = state_r.weight_ema;

    // Responsive (higher alpha) should be closer to target after same number of samples
    // (i.e., responsive EMA > stable EMA when approaching from below)
    ASSERT_TRUE(ema_r > ema_s, "ema_stable_slower_than_responsive");
    PASS("ema_stable_slower_than_responsive");
}

// ============================================================================
// Dead-band filtering
// ============================================================================

static void test_deadband_suppresses_small_changes() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    // Prime at 100g
    feed_constant(state, 100.0f, 50, ms);
    float display_before = state.weight_display;

    // Feed a tiny change (within dead-band)
    const auto &params = scale_smoothing_active_params();
    float tiny = params.deadband * 0.3f;  // well within dead-band
    scale_smoothing_process(state, 100.0f + tiny, ms + 13);

    ASSERT_NEAR(state.weight_display, display_before, 0.001f, "deadband_suppresses_small_changes");
    PASS("deadband_suppresses_small_changes");
}

static void test_deadband_passes_large_changes() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    // Prime at 100g
    feed_constant(state, 100.0f, 50, ms);

    // Feed enough change to exceed dead-band after EMA integration
    feed_constant(state, 101.0f, 50, ms);

    // After 50 more samples at 101g, display should have updated
    ASSERT_TRUE(fabsf(state.weight_display - 101.0f) < 0.2f, "deadband_passes_large_changes");
    PASS("deadband_passes_large_changes");
}

// ============================================================================
// Jump detection
// ============================================================================

static void test_jump_resets_ema_instantly() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    // Stabilize at 100g
    feed_constant(state, 100.0f, 100, ms);
    ASSERT_NEAR(state.weight_ema, 100.0f, 0.01f, "jump_resets_ema_instantly");

    // Sudden jump of 10g (> JUMP_THRESHOLD of 5g)
    ms += 13;
    scale_smoothing_process(state, 110.0f, ms);

    ASSERT_NEAR(state.weight_ema, 110.0f, 0.001f, "jump_resets_ema_instantly");
    ASSERT_NEAR(state.weight_display, 110.0f, 0.001f, "jump_resets_ema_instantly");
    PASS("jump_resets_ema_instantly");
}

static void test_jump_negative_direction() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    feed_constant(state, 200.0f, 100, ms);
    ms += 13;
    scale_smoothing_process(state, 190.0f, ms);  // -10g jump

    ASSERT_NEAR(state.weight_ema, 190.0f, 0.001f, "jump_negative_direction");
    PASS("jump_negative_direction");
}

static void test_no_jump_within_threshold() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    feed_constant(state, 100.0f, 100, ms);
    ms += 13;
    // Change of 4g — below JUMP_THRESHOLD of 5g, should NOT reset
    scale_smoothing_process(state, 104.0f, ms);

    ASSERT_TRUE(state.weight_ema < 104.0f, "no_jump_within_threshold");
    ASSERT_TRUE(state.weight_ema > 100.0f, "no_jump_within_threshold");
    PASS("no_jump_within_threshold");
}

// ============================================================================
// Flow rate calculation
// ============================================================================

static void test_flow_rate_zero_at_rest() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    // Feed constant weight — flow should be ~0
    feed_constant(state, 100.0f, 200, ms);

    ASSERT_NEAR(state.flow_rate, 0.0f, 0.05f, "flow_rate_zero_at_rest");
    PASS("flow_rate_zero_at_rest");
}

static void test_flow_rate_positive_during_pour() {
    scale_smoothing_apply(SCALE_PRESET_RESPONSIVE);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    // Prime at 0g
    feed_constant(state, 0.0f, 20, ms);

    // Simulate ~3 g/s pour for 5 seconds (400 samples at 80 Hz)
    for (int i = 0; i < 400; i++) {
        ms += 13;
        float weight = 0.0f + 3.0f * ((float)ms / 1000.0f);
        scale_smoothing_process(state, weight, ms);
    }

    // Flow rate should be roughly 3 g/s (allow tolerance for EMA lag)
    ASSERT_TRUE(state.flow_rate > 1.5f, "flow_rate_positive_during_pour");
    ASSERT_TRUE(state.flow_rate < 5.0f, "flow_rate_positive_during_pour");
    PASS("flow_rate_positive_during_pour");
}

// ============================================================================
// Reset
// ============================================================================

static void test_reset_clears_state() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    feed_constant(state, 250.0f, 100, ms);
    ASSERT_TRUE(state.ema_primed, "reset_clears_state");
    ASSERT_TRUE(state.ring_count > 0, "reset_clears_state");

    scale_smoothing_reset(state);

    ASSERT_NEAR(state.weight_ema, 0.0f, 0.001f, "reset_clears_state");
    ASSERT_NEAR(state.weight_display, 0.0f, 0.001f, "reset_clears_state");
    ASSERT_NEAR(state.flow_rate, 0.0f, 0.001f, "reset_clears_state");
    ASSERT_TRUE(!state.ema_primed, "reset_clears_state");
    ASSERT_TRUE(state.ring_count == 0, "reset_clears_state");
    PASS("reset_clears_state");
}

static void test_reset_then_new_samples() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    feed_constant(state, 100.0f, 50, ms);
    scale_smoothing_reset(state);

    // New samples after reset should prime fresh
    scale_smoothing_process(state, 200.0f, ms + 13);
    ASSERT_NEAR(state.weight_ema, 200.0f, 0.001f, "reset_then_new_samples");
    ASSERT_TRUE(state.ema_primed, "reset_then_new_samples");
    PASS("reset_then_new_samples");
}

// ============================================================================
// Preset hot-switch
// ============================================================================

static void test_hotswitch_no_state_reset() {
    scale_smoothing_apply(SCALE_PRESET_STABLE);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    feed_constant(state, 100.0f, 100, ms);
    float ema_before = state.weight_ema;
    size_t ring_before = state.ring_count;

    // Switch to Responsive — state should NOT reset
    scale_smoothing_apply(SCALE_PRESET_RESPONSIVE);

    ASSERT_NEAR(state.weight_ema, ema_before, 0.001f, "hotswitch_no_state_reset");
    ASSERT_TRUE(state.ring_count == ring_before, "hotswitch_no_state_reset");
    ASSERT_TRUE(state.ema_primed, "hotswitch_no_state_reset");
    PASS("hotswitch_no_state_reset");
}

static void test_hotswitch_new_alpha_takes_effect() {
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    // Start Stable, prime at 0g
    scale_smoothing_apply(SCALE_PRESET_STABLE);
    feed_constant(state, 0.0f, 20, ms);

    // Switch to Responsive, feed 10g
    scale_smoothing_apply(SCALE_PRESET_RESPONSIVE);
    feed_constant(state, 4.0f, 10, ms);
    float ema_responsive = state.weight_ema;

    // Reset, start Stable again, prime at 0g, feed same 10 samples of 10g
    scale_smoothing_reset(state);
    scale_smoothing_apply(SCALE_PRESET_STABLE);
    ms = 0;
    feed_constant(state, 0.0f, 20, ms);
    feed_constant(state, 4.0f, 10, ms);
    float ema_stable = state.weight_ema;

    // Responsive should have tracked closer to 4g
    ASSERT_TRUE(ema_responsive > ema_stable, "hotswitch_new_alpha_takes_effect");
    PASS("hotswitch_new_alpha_takes_effect");
}

// ============================================================================
// Flow rate settling after weight removal
// ============================================================================

// Helper: simulate weight removal and measure how long flow_rate takes to settle.
// Waits for the spike to develop first (|flow_rate| must exceed spike_min),
// then measures time until |flow_rate| < threshold.
// Returns elapsed ms from removal until settled.
static uint32_t measure_flow_settle_ms(uint8_t preset, float initial_weight, float threshold) {
    scale_smoothing_apply(preset);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    // Stabilize at initial_weight for 3 seconds (well past any window)
    feed_constant(state, initial_weight, 240, ms);  // 240 * 13ms ≈ 3.1s

    // Remove weight (jump detection will snap weight_ema to 0 instantly)
    uint32_t removal_ms = ms;
    feed_constant(state, 0.0f, 1, ms);  // single sample triggers jump

    // Phase 1: wait for the flow spike to develop (|flow| must exceed 1 g/s)
    bool spike_seen = false;
    while ((ms - removal_ms) < 15000) {
        ms += 13;
        scale_smoothing_process(state, 0.0f, ms);
        if (fabsf(state.flow_rate) > 1.0f) {
            spike_seen = true;
            break;
        }
    }
    if (!spike_seen) return 0;  // no spike → settle is instant (good)

    // Phase 2: wait for flow_rate to settle back below threshold
    while ((ms - removal_ms) < 15000) {
        ms += 13;
        scale_smoothing_process(state, 0.0f, ms);
        if (fabsf(state.flow_rate) < threshold) {
            return ms - removal_ms;
        }
    }
    return ms - removal_ms;  // didn't settle in 15s
}

static void test_flow_settles_within_2s_responsive() {
    // Responsive window is 500ms — settle within window + margin
    uint32_t settle = measure_flow_settle_ms(SCALE_PRESET_RESPONSIVE, 200.0f, 0.1f);
    ASSERT_TRUE(settle < 1000, "flow_settles_within_2s_responsive");
    PASS("flow_settles_within_2s_responsive");
}

static void test_flow_settles_within_2s_balanced() {
    // Balanced window is 1000ms — settle within window + margin
    uint32_t settle = measure_flow_settle_ms(SCALE_PRESET_BALANCED, 200.0f, 0.1f);
    ASSERT_TRUE(settle < 1500, "flow_settles_within_2s_balanced");
    PASS("flow_settles_within_2s_balanced");
}

static void test_flow_settles_within_3s_stable() {
    // Stable window is 1500ms — settle within window + margin
    uint32_t settle = measure_flow_settle_ms(SCALE_PRESET_STABLE, 200.0f, 0.1f);
    ASSERT_TRUE(settle < 2500, "flow_settles_within_3s_stable");
    PASS("flow_settles_within_3s_stable");
}

static void test_flow_settles_heavy_weight_within_3s() {
    // Even with 500g, settling should be bounded by flow_window_ms, not weight
    uint32_t settle = measure_flow_settle_ms(SCALE_PRESET_BALANCED, 500.0f, 0.1f);
    ASSERT_TRUE(settle < 1500, "flow_settles_heavy_weight_within_3s");
    PASS("flow_settles_heavy_weight_within_3s");
}

static void test_flow_settle_time_independent_of_weight() {
    // Settling time should NOT scale with the weight removed
    uint32_t settle_200 = measure_flow_settle_ms(SCALE_PRESET_BALANCED, 200.0f, 0.1f);
    uint32_t settle_500 = measure_flow_settle_ms(SCALE_PRESET_BALANCED, 500.0f, 0.1f);
    // They should be nearly identical — within 200ms of each other
    int32_t diff = (int32_t)settle_500 - (int32_t)settle_200;
    if (diff < 0) diff = -diff;
    ASSERT_TRUE(diff < 200, "flow_settle_time_independent_of_weight");
    PASS("flow_settle_time_independent_of_weight");
}

// ============================================================================
// Flow rate dead band
// ============================================================================

static void test_flow_deadband_suppresses_noise_at_rest() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    // Stabilize at 100g
    feed_constant(state, 100.0f, 200, ms);

    // Feed tiny weight changes (within EMA noise range, below jump threshold)
    // that produce small non-zero flow derivatives
    for (int i = 0; i < 100; i++) {
        ms += 13;
        float noise = (i % 2 == 0) ? 0.01f : -0.01f;
        scale_smoothing_process(state, 100.0f + noise, ms);
    }

    // Flow rate should be exactly 0.0 due to dead band, not a tiny non-zero value
    ASSERT_NEAR(state.flow_rate, 0.0f, 0.001f, "flow_deadband_suppresses_noise_at_rest");
    PASS("flow_deadband_suppresses_noise_at_rest");
}

static void test_flow_deadband_passes_real_pour() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    // Prime at 0g
    feed_constant(state, 0.0f, 20, ms);

    // Simulate 3 g/s pour — well above any dead band
    for (int i = 0; i < 400; i++) {
        ms += 13;
        float weight = 3.0f * ((float)ms / 1000.0f);
        scale_smoothing_process(state, weight, ms);
    }

    // Flow rate should be measurably positive (not suppressed by dead band)
    ASSERT_TRUE(state.flow_rate > 1.0f, "flow_deadband_passes_real_pour");
    PASS("flow_deadband_passes_real_pour");
}

static void test_flow_deadband_varies_by_preset() {
    // Stable should have larger dead band than Responsive
    const auto &stable = scale_smoothing_get_params(SCALE_PRESET_STABLE);
    const auto &balanced = scale_smoothing_get_params(SCALE_PRESET_BALANCED);
    const auto &responsive = scale_smoothing_get_params(SCALE_PRESET_RESPONSIVE);

    ASSERT_TRUE(stable.flow_deadband > balanced.flow_deadband, "flow_deadband_varies_by_preset");
    ASSERT_TRUE(balanced.flow_deadband > responsive.flow_deadband, "flow_deadband_varies_by_preset");
    PASS("flow_deadband_varies_by_preset");
}

// ============================================================================
// Negative zero suppression
// ============================================================================

static void test_weight_display_no_negative_zero() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    // Prime with a tiny negative value (simulating load cell drift after tare)
    scale_smoothing_process(state, -0.02f, ms);

    // weight_display should NOT be negative (would show as "-0.0" with %.1f)
    ASSERT_TRUE(state.weight_display >= 0.0f, "weight_display_no_negative_zero");
    PASS("weight_display_no_negative_zero");
}

static void test_weight_display_preserves_real_negatives() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    // Prime with a clearly negative value (e.g., tare drift beyond noise)
    scale_smoothing_process(state, -0.1f, ms);

    // weight_display should preserve the real negative (displays as "-0.1")
    ASSERT_TRUE(state.weight_display < 0.0f, "weight_display_preserves_real_negatives");
    PASS("weight_display_preserves_real_negatives");
}

// ============================================================================
// Ring buffer edge cases
// ============================================================================

static void test_ring_buffer_wraps_correctly() {
    scale_smoothing_apply(SCALE_PRESET_BALANCED);
    ScaleSmoothingState state = {};
    uint32_t ms = 0;

    // Feed more samples than ring capacity (80)
    feed_constant(state, 50.0f, FLOW_RING_CAPACITY + 20, ms);

    ASSERT_TRUE(state.ring_count == FLOW_RING_CAPACITY, "ring_buffer_wraps_correctly");
    ASSERT_NEAR(state.weight_ema, 50.0f, 0.01f, "ring_buffer_wraps_correctly");
    PASS("ring_buffer_wraps_correctly");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("\n-- Preset management --\n");
    test_preset_get_params();
    test_preset_out_of_range_clamps();
    test_preset_apply_changes_active();
    test_preset_apply_out_of_range();

    printf("\n-- EMA priming --\n");
    test_ema_first_sample_seeds();
    test_ema_second_sample_applies_filter();

    printf("\n-- EMA convergence --\n");
    test_ema_converges_to_constant();
    test_ema_stable_slower_than_responsive();

    printf("\n-- Dead-band filtering --\n");
    test_deadband_suppresses_small_changes();
    test_deadband_passes_large_changes();

    printf("\n-- Jump detection --\n");
    test_jump_resets_ema_instantly();
    test_jump_negative_direction();
    test_no_jump_within_threshold();

    printf("\n-- Flow rate --\n");
    test_flow_rate_zero_at_rest();
    test_flow_rate_positive_during_pour();

    printf("\n-- Reset --\n");
    test_reset_clears_state();
    test_reset_then_new_samples();

    printf("\n-- Preset hot-switch --\n");
    test_hotswitch_no_state_reset();
    test_hotswitch_new_alpha_takes_effect();

    printf("\n-- Flow rate settling --\n");
    test_flow_settles_within_2s_responsive();
    test_flow_settles_within_2s_balanced();
    test_flow_settles_within_3s_stable();
    test_flow_settles_heavy_weight_within_3s();
    test_flow_settle_time_independent_of_weight();

    printf("\n-- Flow rate dead band --\n");
    test_flow_deadband_suppresses_noise_at_rest();
    test_flow_deadband_passes_real_pour();
    test_flow_deadband_varies_by_preset();

    printf("\n-- Negative zero suppression --\n");
    test_weight_display_no_negative_zero();
    test_weight_display_preserves_real_negatives();

    printf("\n-- Ring buffer --\n");
    test_ring_buffer_wraps_correctly();

    printf("\n%d/%d tests passed\n\n", g_pass, g_pass + g_fail);
    return g_fail > 0 ? 1 : 0;
}
