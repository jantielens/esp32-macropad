// ============================================================================
// Unit tests for shutter_measure_process_capture()
// ============================================================================
// Tests the stateless computation function directly using synthetic frames.
// No real ADC or LVGL dependencies.

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <cstdlib>

// ---- Host stubs ----
#define IS_SHUTTER_TESTER true
#define SHUTTER_SENSOR_MAX 9
#define SHUTTER_SAMPLE_RATE_HZ 27700
// SHUTTER_DEFAULT_THRESHOLD is defined in shutter_adc.h

// (LOGI/LOGW/LOGE/LOGD are injected via -include tests/log_manager.h)
// (FreeRTOS portMUX stubs are provided via -I tests; see tests/freertos/)

// Serial stub — Arduino Serial used in shutter_measure.cpp
struct SerialStub {
    void println() {}
    void println(const char*) {}
    void printf(const char*, ...) {}
    void print(const char*) {}
} Serial;

// ---- Pull in types from shutter_capture.h and shutter_adc.h ----
#include "../src/app/shutter_adc.h"
#include "../src/app/shutter_capture.h"

// ---- Mock shutter_capture calls used by shutter_measure.cpp ----
static ShutterCaptureFrame g_mock_frame;
static bool g_mock_frame_valid = false;

void shutter_capture_poll() {}
bool shutter_capture_get_latest(ShutterCaptureFrame* out) {
    if (!g_mock_frame_valid || !out) return false;
    *out = g_mock_frame;
    return true;
}

// ---- Include shutter_measure under test ----
// strlcpy — available on ESP32/BSD; declare for older glibc hosts.
#include <stddef.h>
#if !defined(__GLIBC__) || (__GLIBC__ < 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 38)
extern "C" size_t strlcpy(char* dst, const char* src, size_t siz);
#endif
#include "../src/app/shutter_measure.h"

// Stub session hooks — no-ops in test context.
void shutter_session_on_measurement(const ShutterMeasurement*) {}
void shutter_session_on_recompute() {}

bool shutter_capture_is_available() { return true; }
uint8_t shutter_capture_get_positions(ShutterSensorPosition* out, uint8_t max_count) { (void)out; (void)max_count; return 0; }

static ShutterTopologyType g_mock_topology = ShutterTopologyType::ThreeLine;

void shutter_capture_get_caps(ShutterCaptureCaps* out) {
    if (!out) return;
    memset(out, 0, sizeof(ShutterCaptureCaps));
    out->sensor_count = g_mock_frame.sensor_count;
    out->sample_rate_hz_per_sensor = SHUTTER_SAMPLE_RATE_HZ;
    out->topology      = g_mock_topology;
    out->preset_id_str = "direct_3_line";
    out->preset_name   = "3-Line Direct";
    out->backend_name  = "adc_p4";
}
#include "../src/app/shutter_measure.cpp"

// ---- Test harness ----
static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char* label) {
    if (cond) { std::printf("  PASS [%s]\n", label); g_pass++; }
    else       { std::printf("  FAIL [%s]\n", label); g_fail++; }
}

// ---- Helpers to build synthetic waveforms ----

// Allocate a flat waveform (all samples above threshold = no pulse)
// (Not used in current test set; kept for future tests)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static uint16_t* make_flat_waveform(uint32_t count, uint16_t value) {
    uint16_t* buf = (uint16_t*)malloc(count * sizeof(uint16_t));
    for (uint32_t i = 0; i < count; i++) buf[i] = value;
    return buf;
}
#pragma GCC diagnostic pop

// Allocate a waveform with a rectangular pulse:
//   samples before trigger_start: baseline_value (above threshold)
//   samples from trigger_start to trigger_end: low_value (below threshold)
//   samples after trigger_end: baseline_value
static uint16_t* make_pulse_waveform(uint32_t count, uint32_t trigger_start,
                                     uint32_t trigger_end, uint16_t baseline,
                                     uint16_t low_value) {
    uint16_t* buf = (uint16_t*)malloc(count * sizeof(uint16_t));
    for (uint32_t i = 0; i < count; i++) {
        buf[i] = (i >= trigger_start && i < trigger_end) ? low_value : baseline;
    }
    return buf;
}

// Build a ShutterCaptureFrame with n_sensors, each with a pulse of pulse_samples width.
// Sensor i+1 starts its pulse at offset (100 + i*5) to test spread calculations.
static ShutterCaptureFrame make_frame(uint8_t n_sensors, uint32_t total_samples,
                                      uint32_t pulse_samples, uint16_t threshold) {
    ShutterCaptureFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.sensor_count = n_sensors;
    frame.capture_id   = 1;
    frame.timestamp_ms = 1000;
    frame.preset_id    = ShutterPresetId::Direct3Line;
    frame.valid        = true;

    for (int i = 0; i < n_sensors; i++) {
        frame.thresholds[i] = threshold;
        uint32_t start = 100 + (uint32_t)(i * 5);
        uint32_t end   = start + pulse_samples;
        if (end > total_samples) end = total_samples;
        frame.waveforms[i].samples       = make_pulse_waveform(total_samples, start, end, 3000, 500);
        frame.waveforms[i].count         = total_samples;
        frame.waveforms[i].trigger_index = start;
        frame.waveforms[i].sample_rate_hz = (float)SHUTTER_SAMPLE_RATE_HZ;
    }
    return frame;
}

static void free_frame(ShutterCaptureFrame* frame) {
    for (int i = 0; i < frame->sensor_count; i++) {
        free((void*)frame->waveforms[i].samples);
        frame->waveforms[i].samples = nullptr;
    }
}

// ============================================================================
// Test: single-sensor frame produces a valid measurement
// ============================================================================
static void test_single_sensor_valid() {
    std::printf("\n--- test_single_sensor_valid ---\n");

    // 1000 samples at 27700 Hz, pulse of 28 samples ≈ 1.01 ms
    ShutterCaptureFrame frame = make_frame(1, 1000, 28, 2000);
    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(m.valid, "measurement is valid");
    check(m.sensor_count == 1, "sensor_count == 1");
    check(m.valid_sensor_count == 1, "valid_sensor_count == 1");
    check(m.sensors[0].valid, "sensor[0] valid");
    check(m.avg_duration_ms > 0.5f && m.avg_duration_ms < 5.0f, "avg_duration_ms in plausible range");
    check(m.capture_id == 1, "capture_id propagated");
    check(m.preset_id == ShutterPresetId::Direct3Line, "preset_id propagated");
    // Spread not meaningful with 1 sensor
    check(m.spread_ms == 0.0f, "spread_ms == 0 for single sensor");

    free_frame(&frame);
}

// ============================================================================
// Test: 3-sensor frame — count-driven loops use sensor_count
// ============================================================================
static void test_three_sensor_count_driven() {
    std::printf("\n--- test_three_sensor_count_driven ---\n");

    ShutterCaptureFrame frame = make_frame(3, 1000, 28, 2000);
    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(m.valid, "measurement is valid");
    check(m.sensor_count == 3, "sensor_count == 3");
    check(m.valid_sensor_count >= 1, "at least 1 valid sensor");
    // Sensors 4-9 (indices 3-8) should be zeroed (not touched by loops)
    for (int i = 3; i < SHUTTER_SENSOR_MAX; i++) {
        check(!m.sensors[i].valid, "sensors[i].valid == false for i>=3");
    }

    free_frame(&frame);
}

// ============================================================================
// Test: spread is only computed when >= 2 sensors are valid
// ============================================================================
static void test_spread_requires_two_sensors() {
    std::printf("\n--- test_spread_requires_two_sensors ---\n");

    // 1-sensor frame: spread must be 0
    ShutterCaptureFrame f1 = make_frame(1, 1000, 28, 2000);
    ShutterMeasurement m1;
    shutter_measure_process_capture(&f1, &m1);
    check(m1.spread_ms == 0.0f, "spread_ms == 0 with 1 sensor");
    check(m1.spread_pct == 0.0f, "spread_pct == 0 with 1 sensor");
    free_frame(&f1);

    // 3-sensor frame: spread should be > 0 (sensors have different start offsets)
    ShutterCaptureFrame f3 = make_frame(3, 1000, 28, 2000);
    ShutterMeasurement m3;
    shutter_measure_process_capture(&f3, &m3);
    if (m3.valid_sensor_count >= 2) {
        check(m3.spread_ms >= 0.0f, "spread_ms >= 0 with 3 sensors");
    }
    free_frame(&f3);
}

// ============================================================================
// Test: metadata fields are propagated from frame
// ============================================================================
static void test_metadata_propagation() {
    std::printf("\n--- test_metadata_propagation ---\n");

    ShutterCaptureFrame frame = make_frame(3, 1000, 28, 2000);
    frame.capture_id   = 42;
    frame.timestamp_ms = 12345;
    frame.preset_id    = ShutterPresetId::DirectSingle;

    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(m.capture_id == 42, "capture_id == 42");
    check(m.preset_id == ShutterPresetId::DirectSingle, "preset_id propagated");
    check(m.sensor_count == 3, "sensor_count == 3");

    free_frame(&frame);
}

// ============================================================================
// Test: dedup by capture_id in shutter_measure_process()
// ============================================================================
static void test_dedup_by_capture_id() {
    std::printf("\n--- test_dedup_by_capture_id ---\n");
    shutter_measure_init();

    ShutterCaptureFrame frame = make_frame(1, 1000, 28, 2000);
    frame.capture_id = 7;
    g_mock_frame = frame;
    g_mock_frame_valid = true;

    shutter_measure_process();
    uint32_t count_after_first = shutter_measure_get_count();
    check(count_after_first == 1, "first process increments count");

    // Call again with same frame (same capture_id) — should be deduped
    shutter_measure_process();
    uint32_t count_after_second = shutter_measure_get_count();
    check(count_after_second == 1, "second process with same capture_id is deduped");

    // New capture_id: should be processed
    g_mock_frame.capture_id = 8;
    shutter_measure_process();
    uint32_t count_after_third = shutter_measure_get_count();
    check(count_after_third == 2, "new capture_id increments count");

    g_mock_frame_valid = false;
    free_frame(&frame);
}

// ============================================================================
// Test: invalid frame produces m.valid == false
// ============================================================================
static void test_invalid_frame() {
    std::printf("\n--- test_invalid_frame ---\n");
    ShutterCaptureFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.valid = false;

    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);
    check(!m.valid, "null frame yields m.valid == false");

    // Frame with valid=true but sensor_count=0
    frame.valid = true;
    frame.sensor_count = 0;
    shutter_measure_process_capture(&frame, &m);
    check(!m.valid, "zero-sensor frame yields m.valid == false");
}

// ============================================================================
// Test: capping gradient computation
// ============================================================================
static void test_capping_gradient_with_offsets() {
    std::printf("\n--- test_capping_gradient_with_offsets ---\n");

    // Set sensor geometry — diagonal = sqrt((22.4)² + (14.8)²) ≈ 26.85 mm
    ShutterSensorPosition pos3[] = { {-11.2f, -7.4f}, {0.0f, 0.0f}, {+11.2f, +7.4f} };
    shutter_measure_set_geometry(pos3, 3);

    // 3-sensor frame with different pulse widths to create a spread
    ShutterCaptureFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.valid = true;
    frame.sensor_count = 3;
    frame.capture_id = 100;
    frame.timestamp_ms = 1000;
    frame.preset_id = ShutterPresetId::Direct3Line;

    // Sensor 0: 8.0 ms pulse, Sensor 1: 8.2 ms, Sensor 2: 8.4 ms
    // (different start offsets create different durations)
    uint32_t count = 1000;
    float sample_rate = (float)SHUTTER_SAMPLE_RATE_HZ;
    uint16_t thresh = 3000;

    // Compute pulse widths in samples: duration_ms * sample_rate / 1000
    uint32_t pw0 = (uint32_t)(8.0f * sample_rate / 1000.0f);
    uint32_t pw1 = (uint32_t)(8.2f * sample_rate / 1000.0f);
    uint32_t pw2 = (uint32_t)(8.4f * sample_rate / 1000.0f);
    uint32_t start = 200;

    frame.waveforms[0].samples        = make_pulse_waveform(count, start, start + pw0, 3500, 500);
    frame.waveforms[0].count           = count;
    frame.waveforms[0].trigger_index   = start;
    frame.waveforms[0].sample_rate_hz  = sample_rate;
    frame.waveforms[1].samples         = make_pulse_waveform(count, start, start + pw1, 3500, 500);
    frame.waveforms[1].count           = count;
    frame.waveforms[1].trigger_index   = start;
    frame.waveforms[1].sample_rate_hz  = sample_rate;
    frame.waveforms[2].samples         = make_pulse_waveform(count, start, start + pw2, 3500, 500);
    frame.waveforms[2].count           = count;
    frame.waveforms[2].trigger_index   = start;
    frame.waveforms[2].sample_rate_hz  = sample_rate;
    frame.thresholds[0] = thresh;
    frame.thresholds[1] = thresh;
    frame.thresholds[2] = thresh;

    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(m.valid, "gradient: measurement is valid");
    check(m.valid_sensor_count == 3, "gradient: 3 valid sensors");
    check(m.capping_gradient_stops_per_mm >= 0.0f, "gradient: computed (>= 0)");
    check(m.capping_gradient_stops_per_mm < 1.0f, "gradient: reasonable range (< 1)");

    // Verify formula: abs(log2(max/min)) / diagonal
    float expected_diag = 2.0f * sqrtf(11.2f * 11.2f + 7.4f * 7.4f);
    float max_d = m.sensors[2].duration_ms;  // longest
    float min_d = m.sensors[0].duration_ms;  // shortest
    float expected_grad = fabsf(log2f(max_d / min_d)) / expected_diag;
    float diff = fabsf(m.capping_gradient_stops_per_mm - expected_grad);
    check(diff < 0.0001f, "gradient: matches expected formula");

    for (int i = 0; i < 3; i++) free((void*)frame.waveforms[i].samples);

    // Reset geometry
    shutter_measure_set_geometry(nullptr, 0);
}

static void test_capping_gradient_single_sensor() {
    std::printf("\n--- test_capping_gradient_single_sensor ---\n");

    ShutterSensorPosition pos3[] = { {-11.2f, -7.4f}, {0.0f, 0.0f}, {+11.2f, +7.4f} };
    shutter_measure_set_geometry(pos3, 3);

    // Single sensor: gradient should not be computed
    ShutterCaptureFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.valid = true;
    frame.sensor_count = 1;
    frame.capture_id = 101;
    frame.timestamp_ms = 2000;
    frame.preset_id = ShutterPresetId::DirectSingle;

    uint32_t count = 1000;
    float sample_rate = (float)SHUTTER_SAMPLE_RATE_HZ;
    frame.waveforms[0].samples       = make_pulse_waveform(count, 200, 420, 3500, 500);
    frame.waveforms[0].count         = count;
    frame.waveforms[0].trigger_index = 200;
    frame.waveforms[0].sample_rate_hz = sample_rate;
    frame.thresholds[0] = 3000;

    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(m.valid, "gradient single: measurement is valid");
    check(m.capping_gradient_stops_per_mm < 0.0f, "gradient single: sentinel -1 (not computed)");

    free((void*)frame.waveforms[0].samples);
    shutter_measure_set_geometry(nullptr, 0);
}

static void test_capping_gradient_zero_offsets() {
    std::printf("\n--- test_capping_gradient_zero_offsets ---\n");

    // Zero offsets: gradient should not be computed even with 3 sensors
    shutter_measure_set_geometry(nullptr, 0);

    ShutterCaptureFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.valid = true;
    frame.sensor_count = 3;
    frame.capture_id = 102;
    frame.timestamp_ms = 3000;
    frame.preset_id = ShutterPresetId::Direct3Line;

    uint32_t count = 1000;
    float sample_rate = (float)SHUTTER_SAMPLE_RATE_HZ;
    uint32_t pw = (uint32_t)(8.0f * sample_rate / 1000.0f);
    for (int i = 0; i < 3; i++) {
        frame.waveforms[i].samples       = make_pulse_waveform(count, 200, 200 + pw + i * 5, 3500, 500);
        frame.waveforms[i].count         = count;
        frame.waveforms[i].trigger_index = 200;
        frame.waveforms[i].sample_rate_hz = sample_rate;
        frame.thresholds[i] = 3000;
    }

    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(m.valid, "gradient zero offsets: measurement is valid");
    check(m.capping_gradient_stops_per_mm < 0.0f, "gradient zero offsets: sentinel -1 (not computed)");

    for (int i = 0; i < 3; i++) free((void*)frame.waveforms[i].samples);
}

// ============================================================================
// Test: multi-metric verdict — evaluate_verdict()
// ============================================================================

// Helper: build a ShutterMeasurement with specific metric values for verdict testing.
static ShutterMeasurement make_verdict_measurement(float deviation_stops,
                                                    float capping_gradient,
                                                    float spread_pct,
                                                    uint8_t valid_sensor_count) {
    ShutterMeasurement m;
    memset(&m, 0, sizeof(m));
    m.valid = true;
    m.deviation_stops = deviation_stops;
    m.capping_gradient_stops_per_mm = capping_gradient;
    m.capping_gradient_x_stops_per_mm = -1.0f;
    m.capping_gradient_y_stops_per_mm = -1.0f;
    m.skew_differential_us_per_mm = -1.0f;
    m.spread_pct = spread_pct;
    m.valid_sensor_count = valid_sensor_count;
    return m;
}

static void test_verdict_deviation_only() {
    std::printf("\n--- test_verdict_deviation_only ---\n");

    // Pass: deviation within ±1/3 stop
    ShutterMeasurement m = make_verdict_measurement(0.2f, -1.0f, 0.0f, 1);
    check(evaluate_verdict(&m) == SHUTTER_VERDICT_PASS, "deviation 0.2 → pass");

    // Warning: deviation between 1/3 and 1/2 stop
    m = make_verdict_measurement(0.4f, -1.0f, 0.0f, 1);
    check(evaluate_verdict(&m) == SHUTTER_VERDICT_WARNING, "deviation 0.4 → warning");

    // Fail: deviation beyond 1/2 stop
    m = make_verdict_measurement(0.6f, -1.0f, 0.0f, 1);
    check(evaluate_verdict(&m) == SHUTTER_VERDICT_FAIL, "deviation 0.6 → fail");

    // Negative deviation (absolute value used)
    m = make_verdict_measurement(-0.4f, -1.0f, 0.0f, 1);
    check(evaluate_verdict(&m) == SHUTTER_VERDICT_WARNING, "deviation -0.4 → warning");

    // Boundary: exactly at warning threshold — pass (> comparison, not >=)
    m = make_verdict_measurement(0.333f, -1.0f, 0.0f, 1);
    check(evaluate_verdict(&m) == SHUTTER_VERDICT_PASS, "deviation 0.333 → pass (boundary)");
}

static void test_verdict_capping_ignored() {
    std::printf("\n--- test_verdict_capping_ignored ---\n");

    // Capping no longer affects the verdict; only deviation does.
    ShutterMeasurement m = make_verdict_measurement(0.1f, 0.02f, 0.0f, 3);
    check(evaluate_verdict(&m) == SHUTTER_VERDICT_PASS, "large capping alone \u2192 still pass (capping ignored)");

    m = make_verdict_measurement(0.6f, 0.001f, 0.0f, 3);
    check(evaluate_verdict(&m) == SHUTTER_VERDICT_FAIL, "deviation fail + low capping \u2192 fail (deviation drives)");
}

static void test_verdict_combined_capping_ignored() {
    std::printf("\n--- test_verdict_combined_capping_ignored ---\n");

    // Deviation warning + large capping \u2192 still warning (capping ignored).
    ShutterMeasurement m = make_verdict_measurement(0.4f, 0.02f, 0.0f, 3);
    check(evaluate_verdict(&m) == SHUTTER_VERDICT_WARNING, "deviation warn + large capping \u2192 warning (capping ignored)");
}

static void test_verdict_spread_ignored() {
    std::printf("\n--- test_verdict_spread_ignored ---\n");

    // Spread no longer escalates verdict.
    ShutterMeasurement m = make_verdict_measurement(0.1f, -1.0f, 6.0f, 3);
    check(evaluate_verdict(&m) == SHUTTER_VERDICT_PASS, "spread 6% with low deviation \u2192 pass (spread ignored)");

    m = make_verdict_measurement(0.4f, -1.0f, 6.0f, 3);
    check(evaluate_verdict(&m) == SHUTTER_VERDICT_WARNING, "spread 6% + deviation warn \u2192 warning (spread ignored)");
}

// ============================================================================
// Test: false-positive rejection — incident #33 characteristics
// ============================================================================
static void test_false_positive_incident_33() {
    std::printf("\n--- test_false_positive_incident_33 ---\n");

    // Reproduce incident #33 false-positive pattern:
    // S1: short noise blip (duration below floor) — models "marginal, no clear shutter pulse"
    // S2: ambient drift spanning 87% of buffer (coverage filter catches this)
    // S3: single-sample noise blip (duration filter catches this)
    // All three passed the old depth ≥ 100 filter. The new layered filters suppress them.

    float sample_rate = (float)SHUTTER_SAMPLE_RATE_HZ;
    uint32_t total = 10054;  // total samples per sensor from the incident
    uint16_t threshold = 3200;

    ShutterCaptureFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.valid = true;
    frame.sensor_count = 3;
    frame.capture_id = 33;
    frame.timestamp_ms = 5000;
    frame.preset_id = ShutterPresetId::Direct3Line;

    // S1: 0.4 ms noise blip, depth 264.  baseline ~3350, min ~3086
    // Duration below SHUTTER_MIN_PULSE_DURATION_MS (0.45 ms) → rejected.
    uint32_t s1_pw = (uint32_t)(0.4f * sample_rate / 1000.0f);
    uint32_t s1_start = 200;
    frame.waveforms[0].samples       = make_pulse_waveform(total, s1_start, s1_start + s1_pw, 3350, 3086);
    frame.waveforms[0].count         = total;
    frame.waveforms[0].trigger_index = s1_start;
    frame.waveforms[0].sample_rate_hz = sample_rate;
    frame.thresholds[0] = threshold;

    // S2: 422 ms pulse, depth 248. baseline ~3350, min ~3102
    // 422 ms at sample_rate = ~11689 samples — but total is only 10054,
    // so pulse spans from near start to near end: start=755, end=9534 (87% coverage)
    frame.waveforms[1].samples       = make_pulse_waveform(total, 755, 9534, 3350, 3102);
    frame.waveforms[1].count         = total;
    frame.waveforms[1].trigger_index = 755;
    frame.waveforms[1].sample_rate_hz = sample_rate;
    frame.thresholds[1] = threshold;

    // S3: 0.1 ms pulse, depth 126. baseline ~3350, min ~3224
    // 0.1 ms = ~3 samples
    uint32_t s3_start = 4560;
    frame.waveforms[2].samples       = make_pulse_waveform(total, s3_start, s3_start + 3, 3350, 3224);
    frame.waveforms[2].count         = total;
    frame.waveforms[2].trigger_index = s3_start;
    frame.waveforms[2].sample_rate_hz = sample_rate;
    frame.thresholds[2] = threshold;

    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(!m.valid, "incident #33: measurement is suppressed");
    check(m.valid_sensor_count == 0, "incident #33: no valid sensors");

    for (int i = 0; i < 3; i++) free((void*)frame.waveforms[i].samples);
}

// ============================================================================
// Test: 1/2000s pulse accepted (fastest supported speed regression)
// ============================================================================
static void test_fastest_speed_accepted() {
    std::printf("\n--- test_fastest_speed_accepted ---\n");

    // 1/2000s = 0.5 ms, single sensor, depth 500 — must be accepted.
    float sample_rate = (float)SHUTTER_SAMPLE_RATE_HZ;
    uint32_t total = 1000;
    uint32_t pw = (uint32_t)(0.5f * sample_rate / 1000.0f);  // ~14 samples
    uint32_t start = 200;

    ShutterCaptureFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.valid = true;
    frame.sensor_count = 1;
    frame.capture_id = 99;
    frame.timestamp_ms = 9000;
    frame.preset_id = ShutterPresetId::DirectSingle;
    frame.waveforms[0].samples       = make_pulse_waveform(total, start, start + pw, 3500, 3000);
    frame.waveforms[0].count         = total;
    frame.waveforms[0].trigger_index = start;
    frame.waveforms[0].sample_rate_hz = sample_rate;
    frame.thresholds[0] = 3200;

    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(m.valid, "1/2000s: measurement is accepted");
    check(m.valid_sensor_count == 1, "1/2000s: 1 valid sensor");
    check(m.sensors[0].duration_ms > 0.45f, "1/2000s: duration above minimum floor");

    free((void*)frame.waveforms[0].samples);
}

// ============================================================================
// Test: coverage rejection — pulse spanning >80% of buffer
// ============================================================================
static void test_coverage_rejection() {
    std::printf("\n--- test_coverage_rejection ---\n");

    // Pulse spanning 90% of buffer (900 of 1000 samples), depth 500.
    uint32_t total = 1000;
    ShutterCaptureFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.valid = true;
    frame.sensor_count = 1;
    frame.capture_id = 100;
    frame.timestamp_ms = 10000;
    frame.preset_id = ShutterPresetId::DirectSingle;
    frame.waveforms[0].samples       = make_pulse_waveform(total, 50, 950, 3500, 500);
    frame.waveforms[0].count         = total;
    frame.waveforms[0].trigger_index = 50;
    frame.waveforms[0].sample_rate_hz = (float)SHUTTER_SAMPLE_RATE_HZ;
    frame.thresholds[0] = 3200;

    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(!m.valid, "coverage 90%: measurement is suppressed");
    check(!m.sensors[0].valid, "coverage 90%: sensor rejected");

    free((void*)frame.waveforms[0].samples);
}

// ============================================================================
// Test: coherence rejection — wildly different sensor durations
// ============================================================================
static void test_coherence_rejection() {
    std::printf("\n--- test_coherence_rejection ---\n");

    // S1: 8ms pulse, S2: 40ms pulse → ratio 5.0 > 4.0 limit
    float sample_rate = (float)SHUTTER_SAMPLE_RATE_HZ;
    uint32_t total = 2000;
    uint32_t pw1 = (uint32_t)(8.0f * sample_rate / 1000.0f);
    uint32_t pw2 = (uint32_t)(40.0f * sample_rate / 1000.0f);
    uint32_t start = 200;

    ShutterCaptureFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.valid = true;
    frame.sensor_count = 2;
    frame.capture_id = 101;
    frame.timestamp_ms = 11000;
    frame.preset_id = ShutterPresetId::Direct3Line;
    frame.waveforms[0].samples       = make_pulse_waveform(total, start, start + pw1, 3500, 500);
    frame.waveforms[0].count         = total;
    frame.waveforms[0].trigger_index = start;
    frame.waveforms[0].sample_rate_hz = sample_rate;
    frame.thresholds[0] = 3200;
    frame.waveforms[1].samples       = make_pulse_waveform(total, start, start + pw2, 3500, 500);
    frame.waveforms[1].count         = total;
    frame.waveforms[1].trigger_index = start;
    frame.waveforms[1].sample_rate_hz = sample_rate;
    frame.thresholds[1] = 3200;

    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(!m.valid, "coherence ratio 5.0: measurement suppressed");
    check(m.valid_sensor_count == 0, "coherence: all sensors invalidated");

    free((void*)frame.waveforms[0].samples);
    free((void*)frame.waveforms[1].samples);
}

// ============================================================================
// Test: coherence pass — similar sensor durations
// ============================================================================
static void test_coherence_pass() {
    std::printf("\n--- test_coherence_pass ---\n");

    // S1: 8ms, S2: 9ms → ratio 1.125 < 4.0 → accepted
    float sample_rate = (float)SHUTTER_SAMPLE_RATE_HZ;
    uint32_t total = 1000;
    uint32_t pw1 = (uint32_t)(8.0f * sample_rate / 1000.0f);
    uint32_t pw2 = (uint32_t)(9.0f * sample_rate / 1000.0f);
    uint32_t start = 200;

    ShutterCaptureFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.valid = true;
    frame.sensor_count = 2;
    frame.capture_id = 102;
    frame.timestamp_ms = 12000;
    frame.preset_id = ShutterPresetId::Direct3Line;
    frame.waveforms[0].samples       = make_pulse_waveform(total, start, start + pw1, 3500, 500);
    frame.waveforms[0].count         = total;
    frame.waveforms[0].trigger_index = start;
    frame.waveforms[0].sample_rate_hz = sample_rate;
    frame.thresholds[0] = 3200;
    frame.waveforms[1].samples       = make_pulse_waveform(total, start, start + pw2, 3500, 500);
    frame.waveforms[1].count         = total;
    frame.waveforms[1].trigger_index = start;
    frame.waveforms[1].sample_rate_hz = sample_rate;
    frame.thresholds[1] = 3200;

    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(m.valid, "coherence ratio 1.125: measurement accepted");
    check(m.valid_sensor_count == 2, "coherence: both sensors valid");

    free((void*)frame.waveforms[0].samples);
    free((void*)frame.waveforms[1].samples);
}

// ============================================================================
// Main
// ============================================================================
// ============================================================================
// Test: 4-sensor (FourSensor topology) — 2D capping, twist, travel detect
// ============================================================================

// Helper: build a 4-sensor frame with specified pulse widths (ms) and trigger offsets (samples).
static ShutterCaptureFrame make_4sensor_frame(float dur_ms[4], uint32_t start_idx[4]) {
    ShutterCaptureFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.valid = true;
    frame.sensor_count = 4;
    frame.capture_id = 200;
    frame.timestamp_ms = 20000;
    frame.preset_id = ShutterPresetId::Direct4Corner;

    float sample_rate = (float)SHUTTER_SAMPLE_RATE_HZ;
    uint32_t total = 2000;
    uint16_t thresh = 3000;

    for (int i = 0; i < 4; i++) {
        uint32_t pw = (uint32_t)(dur_ms[i] * sample_rate / 1000.0f);
        uint32_t s = start_idx[i];
        frame.waveforms[i].samples       = make_pulse_waveform(total, s, s + pw, 3500, 500);
        frame.waveforms[i].count         = total;
        frame.waveforms[i].trigger_index = s;
        frame.waveforms[i].sample_rate_hz = sample_rate;
        frame.thresholds[i] = thresh;
    }
    return frame;
}

static void test_4sensor_2d_capping() {
    std::printf("\n--- test_4sensor_2d_capping ---\n");

    // Set FourSensor topology. Canonical ordering: TL, TR, BL, BR.
    // col_span = 28mm, row_span = 18mm.
    g_mock_topology = ShutterTopologyType::FourSensor;
    ShutterSensorPosition pos4[] = { {-14.0f, 9.0f}, {14.0f, 9.0f}, {-14.0f, -9.0f}, {14.0f, -9.0f} };
    shutter_measure_set_geometry(pos4, 4);

    // Durations: TL=8ms, TR=9ms, BL=8ms, BR=9ms → horizontal gradient only.
    // e_left = (8+8)/2 = 8, e_right = (9+9)/2 = 9. gx = |log2(9/8)| / 28mm.
    // e_top = (8+9)/2 = 8.5, e_bot = (8+9)/2 = 8.5. gy = 0.
    float dur[4] = {8.0f, 9.0f, 8.0f, 9.0f};
    uint32_t starts[4] = {200, 200, 200, 200};  // simultaneous start
    ShutterCaptureFrame frame = make_4sensor_frame(dur, starts);
    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(m.valid, "4s capping: valid");
    check(m.valid_sensor_count == 4, "4s capping: 4 valid sensors");
    float expected_gx = fabsf(log2f(9.0f / 8.0f)) / 28.0f;
    check(fabsf(m.capping_gradient_x_stops_per_mm - expected_gx) < 0.0001f, "4s capping: gx correct");
    check(m.capping_gradient_y_stops_per_mm < 0.0001f, "4s capping: gy ≈ 0");
    // 1D compat: magnitude = gx (since gy ≈ 0)
    check(fabsf(m.capping_gradient_stops_per_mm - expected_gx) < 0.001f, "4s capping: 1D compat = gx");

    for (int i = 0; i < 4; i++) free((void*)frame.waveforms[i].samples);

    // Reset
    g_mock_topology = ShutterTopologyType::ThreeLine;
    shutter_measure_set_geometry(nullptr, 0);
}

static void test_4sensor_skew_differential() {
    std::printf("\n--- test_4sensor_skew_differential ---\n");

    g_mock_topology = ShutterTopologyType::FourSensor;
    ShutterSensorPosition pos4[] = { {-14.0f, 9.0f}, {14.0f, 9.0f}, {-14.0f, -9.0f}, {14.0f, -9.0f} };
    shutter_measure_set_geometry(pos4, 4);

    // Uniform duration. Stagger start times to simulate curtain tilt:
    // Top row: S0 starts at 200, S1 at 210 → skew_top = (210-200)/27700 / 28mm * 1e6
    // Bot row: S2 starts at 200, S3 at 220 → skew_bot = (220-200)/27700 / 28mm * 1e6
    // twist = skew_bot - skew_top
    float dur[4] = {8.0f, 8.0f, 8.0f, 8.0f};
    uint32_t starts[4] = {200, 210, 200, 220};
    ShutterCaptureFrame frame = make_4sensor_frame(dur, starts);
    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(m.valid, "4s twist: valid");
    float rate = (float)SHUTTER_SAMPLE_RATE_HZ;
    float skew_top = ((210.0f - 200.0f) / rate) / 28.0f * 1e6f;
    float skew_bot = ((220.0f - 200.0f) / rate) / 28.0f * 1e6f;
    float expected_twist = skew_bot - skew_top;
    check(fabsf(m.skew_differential_us_per_mm - expected_twist) < 0.001f, "4s skew_diff: value correct");

    // Per-curtain per-position skew fields.
    // Curtain 1 (opening) uses start_idx. Left pair: S0(TL)=200, S2(BL)=200 → 0.
    // Right pair: S1(TR)=210, S3(BR)=220 → (220-210)/rate * 1e6.
    float expected_c1_skew_left  = 0.0f;
    float expected_c1_skew_right = (220.0f - 210.0f) / rate * 1e6f;
    check(fabsf(m.curtain1_skew_left_us  - expected_c1_skew_left)  < 0.01f, "4s c1 skew left");
    check(fabsf(m.curtain1_skew_right_us - expected_c1_skew_right) < 0.01f, "4s c1 skew right");
    // Curtain 2 (closing) uses end_idx. With uniform duration, offsets are the same.
    check(fabsf(m.curtain2_skew_left_us  - expected_c1_skew_left)  < 0.01f, "4s c2 skew left");
    check(fabsf(m.curtain2_skew_right_us - expected_c1_skew_right) < 0.01f, "4s c2 skew right");

    for (int i = 0; i < 4; i++) free((void*)frame.waveforms[i].samples);
    g_mock_topology = ShutterTopologyType::ThreeLine;
    shutter_measure_set_geometry(nullptr, 0);
}

static void test_4sensor_skew_vertical_travel() {
    std::printf("\n--- test_4sensor_skew_vertical_travel ---\n");

    g_mock_topology = ShutterTopologyType::FourSensor;
    ShutterSensorPosition pos4[] = { {-14.0f, 9.0f}, {14.0f, 9.0f}, {-14.0f, -9.0f}, {14.0f, -9.0f} };
    shutter_measure_set_geometry(pos4, 4);

    // Vertical travel: large row delay (300 samples), small column skew (10 samples).
    // TL=200, TR=210, BL=500, BR=510 → dt_rows=300 >> dt_cols=10 → detected V.
    float dur[4] = {8.0f, 8.0f, 8.0f, 8.0f};
    uint32_t starts[4] = {200, 210, 500, 510};
    ShutterCaptureFrame frame = make_4sensor_frame(dur, starts);
    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(m.valid, "4s V skew: valid");
    check(strcmp(m.detected_travel, "V") == 0, "4s V skew: detected V");

    // For V travel, cross-axis = X: skew_left = BR-BL (bottom row), skew_right = TR-TL (top row).
    float rate = (float)SHUTTER_SAMPLE_RATE_HZ;
    float expected_skew_left  = (510.0f - 500.0f) / rate * 1e6f;  // BR - BL
    float expected_skew_right = (210.0f - 200.0f) / rate * 1e6f;  // TR - TL
    check(fabsf(m.curtain1_skew_left_us  - expected_skew_left)  < 0.01f, "4s V c1 skew left (BR-BL)");
    check(fabsf(m.curtain1_skew_right_us - expected_skew_right) < 0.01f, "4s V c1 skew right (TR-TL)");
    // Verify values are small cross-axis differences (~361µs), NOT large travel delays (~10800µs).
    check(fabsf(m.curtain1_skew_left_us)  < 500.0f, "4s V c1 skew left is cross-axis (small)");
    check(fabsf(m.curtain1_skew_right_us) < 500.0f, "4s V c1 skew right is cross-axis (small)");

    for (int i = 0; i < 4; i++) free((void*)frame.waveforms[i].samples);
    g_mock_topology = ShutterTopologyType::ThreeLine;
    shutter_measure_set_geometry(nullptr, 0);
}

static void test_4sensor_detect_vertical_travel() {
    std::printf("\n--- test_4sensor_detect_vertical_travel ---\n");

    g_mock_topology = ShutterTopologyType::FourSensor;
    ShutterSensorPosition pos4[] = { {-14.0f, 9.0f}, {14.0f, 9.0f}, {-14.0f, -9.0f}, {14.0f, -9.0f} };
    shutter_measure_set_geometry(pos4, 4);

    // Vertical travel: top row triggers much earlier than bottom row.
    // Columns trigger at same time within each row.
    float dur[4] = {8.0f, 8.0f, 8.0f, 8.0f};
    uint32_t starts[4] = {200, 200, 500, 500};  // dt_rows >> dt_cols
    ShutterCaptureFrame frame = make_4sensor_frame(dur, starts);
    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(m.valid, "4s detect V: valid");
    check(strcmp(m.detected_travel, "V") == 0, "4s detect V: detected_travel = V");

    for (int i = 0; i < 4; i++) free((void*)frame.waveforms[i].samples);

    // Horizontal travel: columns trigger far apart, rows same.
    uint32_t starts_h[4] = {200, 500, 200, 500};  // dt_cols >> dt_rows
    frame = make_4sensor_frame(dur, starts_h);
    shutter_measure_process_capture(&frame, &m);
    check(strcmp(m.detected_travel, "H") == 0, "4s detect H: detected_travel = H");
    for (int i = 0; i < 4; i++) free((void*)frame.waveforms[i].samples);

    // Leaf/simultaneous: all start at same time.
    uint32_t starts_l[4] = {200, 200, 200, 200};
    frame = make_4sensor_frame(dur, starts_l);
    shutter_measure_process_capture(&frame, &m);
    check(strcmp(m.detected_travel, "L") == 0, "4s detect L: detected_travel = L");
    for (int i = 0; i < 4; i++) free((void*)frame.waveforms[i].samples);

    g_mock_topology = ShutterTopologyType::ThreeLine;
    shutter_measure_set_geometry(nullptr, 0);
}

static void test_4sensor_verdict_2d_extrapolation() {
    std::printf("\n--- test_4sensor_verdict_2d_extrapolation ---\n");

    g_mock_topology = ShutterTopologyType::FourSensor;
    ShutterSensorPosition pos4[] = { {-14.0f, 9.0f}, {14.0f, 9.0f}, {-14.0f, -9.0f}, {14.0f, -9.0f} };
    shutter_measure_set_geometry(pos4, 4);

    // Create a moderate horizontal gradient that produces frame capping above warning (0.333 stops).
    // Need gx * 36 > 0.333 → gx > 0.00925. Use durations that produce gx = |log2(10/8)|/28 = 0.0116.
    float dur[4] = {8.0f, 10.0f, 8.0f, 10.0f};
    uint32_t starts[4] = {200, 200, 200, 200};
    ShutterCaptureFrame frame = make_4sensor_frame(dur, starts);
    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(m.valid, "4s verdict: valid");
    // Capping no longer drives verdict; only deviation does. With no nominal set,
    // deviation_stops = 0 → verdict should be PASS regardless of horizontal gradient.
    check(evaluate_verdict(&m) == SHUTTER_VERDICT_PASS, "4s verdict: horizontal capping ignored → pass");

    for (int i = 0; i < 4; i++) free((void*)frame.waveforms[i].samples);
    g_mock_topology = ShutterTopologyType::ThreeLine;
    shutter_measure_set_geometry(nullptr, 0);
}

static void test_3sensor_regression_new_fields() {
    std::printf("\n--- test_3sensor_regression_new_fields ---\n");

    // Ensure 3-sensor frames leave 4-sensor fields at sentinel values.
    ShutterSensorPosition pos3[] = { {-11.2f, -7.4f}, {0.0f, 0.0f}, {+11.2f, +7.4f} };
    shutter_measure_set_geometry(pos3, 3);

    ShutterCaptureFrame frame = make_frame(3, 1000, 28, 2000);
    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);

    check(m.capping_gradient_x_stops_per_mm < 0.0f, "3s regression: cap_x sentinel");
    check(m.capping_gradient_y_stops_per_mm < 0.0f, "3s regression: cap_y sentinel");
    check(m.skew_differential_us_per_mm < 0.0f, "3s regression: skew_diff sentinel");
    check(m.curtain1_skew_left_us  == 0.0f, "3s regression: c1 skew left sentinel");
    check(m.curtain1_skew_right_us == 0.0f, "3s regression: c1 skew right sentinel");
    check(m.curtain2_skew_left_us  == 0.0f, "3s regression: c2 skew left sentinel");
    check(m.curtain2_skew_right_us == 0.0f, "3s regression: c2 skew right sentinel");
    check(m.detected_travel[0] == '\0', "3s regression: detected_travel empty");

    free_frame(&frame);
    shutter_measure_set_geometry(nullptr, 0);
}

// ============================================================================
// Test: slope-fit edge extrapolation is brightness invariant for clean
// single-pole RC pulses.
//
// Synthesize a rectangular slit pulse passed through a first-order RC filter
// at three different brightnesses (depth D). For ideal RC, the slope-fit
// extrapolation to the 50% point should give an identical duration regardless
// of D.
// ============================================================================
static uint16_t* make_rc_pulse_waveform(uint32_t count, uint32_t pulse_start,
                                         uint32_t pulse_end, uint16_t baseline,
                                         uint16_t depth_amplitude,
                                         float tau_samples) {
    // V(i) = baseline - depth(i)
    // depth(i) follows step response: rises during pulse, falls after.
    // During pulse: depth(i) = D * (1 - exp(-(i - pulse_start)/tau))
    // After pulse:  depth(i) = D_peak * exp(-(i - pulse_end)/tau)
    uint16_t* buf = (uint16_t*)malloc(count * sizeof(uint16_t));
    float D_peak = (float)depth_amplitude *
                   (1.0f - expf(-(float)(pulse_end - pulse_start) / tau_samples));
    for (uint32_t i = 0; i < count; i++) {
        float depth = 0.0f;
        if (i < pulse_start) {
            depth = 0.0f;
        } else if (i < pulse_end) {
            depth = (float)depth_amplitude *
                    (1.0f - expf(-((float)i - (float)pulse_start) / tau_samples));
        } else {
            depth = D_peak * expf(-((float)i - (float)pulse_end) / tau_samples);
        }
        int v = (int)baseline - (int)(depth + 0.5f);
        if (v < 0) v = 0;
        if (v > 4095) v = 4095;
        buf[i] = (uint16_t)v;
    }
    return buf;
}

static float measure_rc_pulse_duration(uint32_t pulse_samples, uint16_t depth,
                                        float tau_samples) {
    const uint32_t count = 2000;
    const uint32_t pulse_start = 400;
    const uint32_t pulse_end   = pulse_start + pulse_samples;

    ShutterCaptureFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.valid        = true;
    frame.sensor_count = 1;
    frame.capture_id   = 9001;
    frame.timestamp_ms = 1000;
    frame.preset_id    = ShutterPresetId::DirectSingle;
    frame.thresholds[0]              = 3000;
    frame.waveforms[0].samples       = make_rc_pulse_waveform(count, pulse_start, pulse_end,
                                                              3500, depth, tau_samples);
    frame.waveforms[0].count         = count;
    frame.waveforms[0].trigger_index = pulse_start;
    frame.waveforms[0].sample_rate_hz = (float)SHUTTER_SAMPLE_RATE_HZ;

    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);
    float dur = m.sensors[0].valid ? m.sensors[0].duration_ms : -1.0f;
    free_frame(&frame);
    return dur;
}

static void test_slope_fit_brightness_invariance() {
    std::printf("\n--- test_slope_fit_brightness_invariance ---\n");

    // Pulse: 28 samples ≈ 1.01 ms at 27700 Hz, RC tau = 3 samples.
    // Three brightnesses (depth amplitudes). For an ideal single-pole filter
    // the slope-fit-to-50% duration should be identical regardless of depth.
    float d1 = measure_rc_pulse_duration(28, 800,  3.0f);
    float d2 = measure_rc_pulse_duration(28, 1600, 3.0f);
    float d3 = measure_rc_pulse_duration(28, 2400, 3.0f);

    std::printf("  durations: d=800 -> %.4f ms, d=1600 -> %.4f ms, d=2400 -> %.4f ms\n",
                d1, d2, d3);

    check(d1 > 0.0f && d2 > 0.0f && d3 > 0.0f, "all three durations measured");
    // Allow 1% spread across brightness range. Real RC pulses through an
    // ideal filter should give identical durations; the tolerance covers
    // ADC quantization of the synthetic waveform (uint16 rounding shifts
    // the 20-80% fit window samples by a fraction of a sample).
    float ref = d2;
    check(fabsf(d1 - ref) / ref < 0.01f, "d=800 within 1% of d=1600");
    check(fabsf(d3 - ref) / ref < 0.01f, "d=2400 within 1% of d=1600");
}

// Slew-rate-limited edge: simulates an amplifier that cannot follow the input
// step faster than a fixed dV/dt. The deepest portion is clipped (flat
// plateau at the slew-reachable depth during the slit). Slope-fit should
// recover the slit duration; the legacy 50% midpoint method would over- or
// underestimate it depending on where the 50% point lands relative to the
// slew-limited region.
static uint16_t* make_slew_limited_waveform(uint32_t count, uint32_t pulse_start,
                                             uint32_t pulse_end, uint16_t baseline,
                                             float slew_per_sample) {
    uint16_t* buf = (uint16_t*)malloc(count * sizeof(uint16_t));
    float v = (float)baseline;
    float target = (float)baseline;
    for (uint32_t i = 0; i < count; i++) {
        // Target jumps down at pulse_start, back up at pulse_end.
        if (i == pulse_start) target = (float)baseline - 1500.0f;
        if (i == pulse_end)   target = (float)baseline;
        // Slew towards target.
        if (v < target) v = fminf(v + slew_per_sample, target);
        else if (v > target) v = fmaxf(v - slew_per_sample, target);
        int iv = (int)(v + 0.5f);
        if (iv < 0) iv = 0;
        if (iv > 4095) iv = 4095;
        buf[i] = (uint16_t)iv;
    }
    return buf;
}

static void test_slope_fit_slew_limited_edges() {
    std::printf("\n--- test_slope_fit_slew_limited_edges ---\n");

    const uint32_t count = 2000;
    const uint32_t pulse_start = 400;
    const uint32_t pulse_samples = 28;             // ~1.01 ms
    const uint32_t pulse_end   = pulse_start + pulse_samples;
    const float expected_ms = (float)pulse_samples / (float)SHUTTER_SAMPLE_RATE_HZ * 1000.0f;

    ShutterCaptureFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.valid        = true;
    frame.sensor_count = 1;
    frame.capture_id   = 9002;
    frame.timestamp_ms = 1000;
    frame.preset_id    = ShutterPresetId::DirectSingle;
    frame.thresholds[0]              = 3000;
    frame.waveforms[0].samples       = make_slew_limited_waveform(count, pulse_start, pulse_end,
                                                                  3500, 250.0f);
    frame.waveforms[0].count         = count;
    frame.waveforms[0].trigger_index = pulse_start;
    frame.waveforms[0].sample_rate_hz = (float)SHUTTER_SAMPLE_RATE_HZ;

    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);
    check(m.sensors[0].valid, "slew-limited: valid measurement");
    float err = fabsf(m.sensors[0].duration_ms - expected_ms);
    std::printf("  slew-limited duration = %.4f ms (expected %.4f, err %.4f)\n",
                m.sensors[0].duration_ms, expected_ms, err);
    // Slope-fit through the linear slew region should reproduce T_slit within
    // one sample period (~0.036 ms at 27.7 kHz).
    check(err < 0.05f, "slew-limited duration within 0.05 ms of slit time");

    free_frame(&frame);
}

int main() {
    std::printf("=== test_shutter_measure ===\n");

    test_single_sensor_valid();
    test_three_sensor_count_driven();
    test_spread_requires_two_sensors();
    test_metadata_propagation();
    test_dedup_by_capture_id();
    test_invalid_frame();
    test_capping_gradient_with_offsets();
    test_capping_gradient_single_sensor();
    test_capping_gradient_zero_offsets();
    test_verdict_deviation_only();
    test_verdict_capping_ignored();
    test_verdict_combined_capping_ignored();
    test_verdict_spread_ignored();
    test_false_positive_incident_33();
    test_fastest_speed_accepted();
    test_coverage_rejection();
    test_coherence_rejection();
    test_coherence_pass();
    test_4sensor_2d_capping();
    test_4sensor_skew_differential();
    test_4sensor_skew_vertical_travel();
    test_4sensor_detect_vertical_travel();
    test_4sensor_verdict_2d_extrapolation();
    test_3sensor_regression_new_fields();
    test_slope_fit_brightness_invariance();
    test_slope_fit_slew_limited_edges();

    std::printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
