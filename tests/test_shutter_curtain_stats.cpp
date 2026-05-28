// ============================================================================
// Host-native unit tests for compute_curtain_stats() validity gating
// ============================================================================
//
// Covers the two-tier validity model:
//   - edges_detected ⇒ JSON object emitted (timing fields populated)
//   - valid          ⇒ curtain_ratio is physically meaningful
//
// Gate #1 (full-open mode):     dwell > MIN_SLIT_DWELL_RATIO * max(c1, c2)
// Gate #2 (close-edge recovery): close_width > MAX_CLOSE_EDGE_WIDTH_RATIO * open_width

#include "shutter_curtain_stats.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static constexpr float SAMPLE_RATE_HZ = 13800.0f;
static constexpr float MS_PER_SAMPLE  = 1000.0f / SAMPLE_RATE_HZ;
static constexpr float BASELINE       = 3000.0f;
static constexpr float TROUGH         = 500.0f;

// Build a synthetic inverted-pulse waveform.
//   - leading `pre_samples` of baseline
//   - opening edge linearly drops from BASELINE → TROUGH over `open_samples`
//   - dwell holds at TROUGH for `dwell_samples`
//   - closing edge linearly rises from TROUGH → BASELINE over `close_samples`
//   - trailing baseline back to BASELINE for `tail_samples`
static std::vector<uint16_t> build_pulse(uint32_t pre_samples,
                                         uint32_t open_samples,
                                         uint32_t dwell_samples,
                                         uint32_t close_samples,
                                         uint32_t tail_samples) {
    std::vector<uint16_t> w;
    w.reserve(pre_samples + open_samples + dwell_samples + close_samples + tail_samples);
    for (uint32_t i = 0; i < pre_samples; i++) w.push_back((uint16_t)BASELINE);
    for (uint32_t i = 0; i < open_samples; i++) {
        float t = (float)(i + 1) / (float)open_samples;
        w.push_back((uint16_t)(BASELINE + (TROUGH - BASELINE) * t));
    }
    for (uint32_t i = 0; i < dwell_samples; i++) w.push_back((uint16_t)TROUGH);
    for (uint32_t i = 0; i < close_samples; i++) {
        float t = (float)(i + 1) / (float)close_samples;
        w.push_back((uint16_t)(TROUGH + (BASELINE - TROUGH) * t));
    }
    for (uint32_t i = 0; i < tail_samples; i++) w.push_back((uint16_t)BASELINE);
    return w;
}

static int failures = 0;
#define EXPECT(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

// --- Slit-mode (1/250s-ish): short dwell, symmetric edges ---
static void test_slit_mode_valid() {
    // open=8 samples, dwell=4 samples, close=8 samples. max(c1,c2)=8,
    // dwell/max = 0.5 → below MIN_SLIT_DWELL_RATIO (4.0). close/open=1.0.
    auto w = build_pulse(20, 8, 4, 8, 20);
    auto s = compute_curtain_stats(w.data(), (uint32_t)w.size(), SAMPLE_RATE_HZ, BASELINE);
    EXPECT(s.edges_detected);
    EXPECT(s.valid);
    EXPECT(s.curtain1_ms > 0.0f);
    EXPECT(s.curtain2_ms > 0.0f);
    EXPECT(std::fabs(s.curtain_ratio - 1.0f) < 0.2f);
}

// --- Full-open mode (1/4s-ish): long dwell triggers Gate #1 ---
static void test_full_open_invalid() {
    // open=8, dwell=200, close=8. max(c1,c2)=8 samples ≈ 0.58 ms.
    // Edge widths (10–90%) ≈ 6.4 samples → dwell ≈ 200 / 6.4 ≈ 31× edge → triggers Gate #1.
    auto w = build_pulse(20, 8, 200, 8, 20);
    auto s = compute_curtain_stats(w.data(), (uint32_t)w.size(), SAMPLE_RATE_HZ, BASELINE);
    EXPECT(s.edges_detected);
    EXPECT(!s.valid);                  // Gate #1 fired
    EXPECT(s.curtain1_ms > 0.0f);      // raw fields still populated
    EXPECT(s.dwell_ms    > 0.0f);
    EXPECT(s.curtain2_ms > 0.0f);
}

// --- Sensor recovery tail (sess_91 sensor 4): asymmetric close edge ---
static void test_recovery_tail_invalid() {
    // open=8, dwell=10, close=80. Close edge ≈ 10× open edge → Gate #2 fires.
    // dwell ratio = 10/80 = 0.125 → Gate #1 NOT fired.
    auto w = build_pulse(20, 8, 10, 80, 20);
    auto s = compute_curtain_stats(w.data(), (uint32_t)w.size(), SAMPLE_RATE_HZ, BASELINE);
    EXPECT(s.edges_detected);
    EXPECT(!s.valid);                  // Gate #2 fired
    EXPECT(s.curtain2_ms > s.curtain1_ms * 3.0f);  // confirms asymmetry
}

// --- No swing: flat baseline → edges_detected false ---
static void test_no_swing() {
    std::vector<uint16_t> w(200, (uint16_t)BASELINE);
    auto s = compute_curtain_stats(w.data(), (uint32_t)w.size(), SAMPLE_RATE_HZ, BASELINE);
    EXPECT(!s.edges_detected);
    EXPECT(!s.valid);
}

// --- Zero baseline: invalid contract → edges_detected false ---
static void test_zero_baseline() {
    auto w = build_pulse(20, 8, 4, 8, 20);
    auto s = compute_curtain_stats(w.data(), (uint32_t)w.size(), SAMPLE_RATE_HZ, 0.0f);
    EXPECT(!s.edges_detected);
    EXPECT(!s.valid);
}

// --- Null / short / bad-rate inputs ---
static void test_input_guards() {
    auto s1 = compute_curtain_stats(nullptr, 100, SAMPLE_RATE_HZ, BASELINE);
    EXPECT(!s1.edges_detected);
    uint16_t tiny[2] = {3000, 3000};
    auto s2 = compute_curtain_stats(tiny, 2, SAMPLE_RATE_HZ, BASELINE);
    EXPECT(!s2.edges_detected);
    auto w = build_pulse(20, 8, 4, 8, 20);
    auto s3 = compute_curtain_stats(w.data(), (uint32_t)w.size(), 0.0f, BASELINE);
    EXPECT(!s3.edges_detected);
}

int main() {
    test_slit_mode_valid();
    test_full_open_invalid();
    test_recovery_tail_invalid();
    test_no_swing();
    test_zero_baseline();
    test_input_guards();

    if (failures == 0) {
        std::printf("All shutter_curtain_stats tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) failed.\n", failures);
    return 1;
}
