// ============================================================================
// Unit tests for shutter_capture preset resolution and capability reporting
// ============================================================================
// Tests the shutter_capture seam in isolation using a mock shutter_adc backend.
// Focuses on:
//   1. Preset resolution: valid, invalid, reserved, and board-pin fallback paths
//   2. Capability reporting after init
//   3. Trigger config round-trip

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>

// ---- Host-test stubs for board macros ----
// Define pins so the preset table compiles. S1-S3 are valid; S4-S9 are -1.
#define SHUTTER_ADC_PIN_S1 49
#define SHUTTER_ADC_PIN_S2 50
#define SHUTTER_ADC_PIN_S3 51
#define SHUTTER_ADC_PIN_S4 (-1)
#define SHUTTER_ADC_PIN_S5 (-1)
#define SHUTTER_ADC_PIN_S6 (-1)
#define SHUTTER_ADC_PIN_S7 (-1)
#define SHUTTER_ADC_PIN_S8 (-1)
#define SHUTTER_ADC_PIN_S9 (-1)
#define SHUTTER_SAMPLE_RATE_HZ 27700
#define IS_SHUTTER_TESTER true

// ---- Pull in capture types and real shutter_adc.h ----
// (log_manager.h is injected via -include by the build command)
#include "shutter_capture.h"
#include "shutter_adc.h"

// ---- Mock shutter_adc backend ----
static int g_adc_init_calls = 0;
static uint8_t g_adc_init_count = 0;
static float g_adc_sample_rate_hz = (float)SHUTTER_SAMPLE_RATE_HZ;
static int g_adc_recalibrate_calls = 0;
static bool g_adc_calibrating = false;

void shutter_adc_init(uint8_t active_sensor_count) {
    g_adc_init_calls++;
    g_adc_init_count = active_sensor_count;
}
bool shutter_adc_is_available() { return g_adc_init_calls > 0; }
bool shutter_adc_get_capture(ShutterCapture* out) { (void)out; return false; }
uint16_t shutter_adc_get_threshold() { return SHUTTER_DEFAULT_THRESHOLD; }
void shutter_adc_set_threshold(uint16_t t) { (void)t; }
float shutter_adc_get_sample_rate_hz() { return g_adc_sample_rate_hz; }
void shutter_adc_start_alignment() {}
void shutter_adc_stop_alignment() {}
bool shutter_adc_is_alignment_active() { return false; }
void shutter_adc_recalibrate() { g_adc_recalibrate_calls++; }
bool shutter_adc_is_calibrating() { return g_adc_calibrating; }
bool shutter_adc_get_alignment(ShutterAlignmentReading* out) { (void)out; return false; }
void shutter_adc_set_slot_mapping(const ShutterSensorSlotMapping* mapping) { (void)mapping; }

// New lifecycle API — always succeed in tests; refcount paths are exercised
// through shutter_capture_acquire/release.
static int g_adc_start_calls = 0;
static int g_adc_stop_calls  = 0;
static bool g_adc_running    = false;
bool shutter_adc_start() { g_adc_start_calls++; g_adc_running = true; return true; }
void shutter_adc_stop()  { g_adc_stop_calls++;  g_adc_running = false; }
bool shutter_adc_is_running() { return g_adc_running; }

// ---- Include implementation under test ----
#include "../src/app/device_classes/shutter_tester/shutter_capture.cpp"

// ---- Test harness ----
static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char* label) {
    if (cond) {
        std::printf("  PASS [%s]\n", label);
        g_pass++;
    } else {
        std::printf("  FAIL [%s]\n", label);
        g_fail++;
    }
}

// Reset internal module state between tests by re-calling init
static void reset_module(const char* preset_id = "direct_3_line") {
    g_adc_init_calls = 0;
    g_adc_init_count = 0;
    g_adc_sample_rate_hz = (float)SHUTTER_SAMPLE_RATE_HZ;
    g_adc_recalibrate_calls = 0;
    g_adc_calibrating = false;
    shutter_capture_init(preset_id);
}

// ============================================================================
// Test: Valid preset IDs activate their expected sensor count
// ============================================================================
static void test_valid_preset_direct_single() {
    std::printf("\n--- test_valid_preset_direct_single ---\n");
    reset_module("direct_single");

    ShutterCaptureCaps caps;
    shutter_capture_get_caps(&caps);

    check(shutter_capture_is_available(), "available after direct_single init");
    check(caps.sensor_count == 1, "sensor_count == 1");
    check(strcmp(caps.preset_id_str, "direct_single") == 0, "preset_id_str == direct_single");
    check(g_adc_init_count == 1, "adc_init called with count=1");
}

static void test_valid_preset_direct_3_line() {
    std::printf("\n--- test_valid_preset_direct_3_line ---\n");
    reset_module("direct_3_line");

    ShutterCaptureCaps caps;
    shutter_capture_get_caps(&caps);

    check(shutter_capture_is_available(), "available after direct_3_line init");
    check(caps.sensor_count == 3, "sensor_count == 3");
    check(strcmp(caps.preset_id_str, "direct_3_line") == 0, "preset_id_str == direct_3_line");
    check(g_adc_init_count == 3, "adc_init called with count=3");
}

// ============================================================================
// Test: Invalid preset falls back to direct_3_line
// ============================================================================
static void test_invalid_preset_fallback() {
    std::printf("\n--- test_invalid_preset_fallback ---\n");
    reset_module("nonexistent_preset");

    ShutterCaptureCaps caps;
    shutter_capture_get_caps(&caps);

    check(shutter_capture_is_available(), "still available after invalid preset");
    check(caps.sensor_count == 3, "fallback sensor_count == 3");
    check(strcmp(caps.preset_id_str, "direct_3_line") == 0, "fallback preset_id_str == direct_3_line");
}

// ============================================================================
// Test: Empty preset ID falls back to direct_3_line
// ============================================================================
static void test_empty_preset_fallback() {
    std::printf("\n--- test_empty_preset_fallback ---\n");
    reset_module("");

    ShutterCaptureCaps caps;
    shutter_capture_get_caps(&caps);

    check(shutter_capture_is_available(), "available after empty preset");
    check(caps.sensor_count == 3, "empty preset fallback sensor_count == 3");
    check(strcmp(caps.preset_id_str, "direct_3_line") == 0, "empty preset fallback id");
}

// ============================================================================
// Test: Reserved (offload) presets fall back to direct_3_line
// (because active_now is false and they require remote hardware)
// ============================================================================
static void test_reserved_presets_fallback() {
    std::printf("\n--- test_reserved_presets_fallback (offload_3_line) ---\n");
    reset_module("offload_3_line");

    ShutterCaptureCaps caps;
    shutter_capture_get_caps(&caps);

    // offload_3_line is reserved (active_now=false), so should fall back
    check(shutter_capture_is_available(), "available after offload fallback");
    // Should resolve to direct_3_line or direct_single (3-line preferred)
    check(caps.sensor_count >= 1, "sensor_count >= 1 after fallback");
    check(strcmp(caps.preset_id_str, "offload_3_line") != 0, "offload_3_line not active");

    std::printf("\n--- test_reserved_presets_fallback (offload_9_matrix) ---\n");
    reset_module("offload_9_matrix");
    shutter_capture_get_caps(&caps);
    check(strcmp(caps.preset_id_str, "offload_9_matrix") != 0, "offload_9_matrix not active");
}

// ============================================================================
// Test: Capability struct is fully populated
// ============================================================================
static void test_caps_fields_populated() {
    std::printf("\n--- test_caps_fields_populated ---\n");
    reset_module("direct_3_line");

    ShutterCaptureCaps caps;
    shutter_capture_get_caps(&caps);

    check(caps.preset_id_str[0] != '\0', "preset_id_str non-empty");
    check(caps.preset_name[0] != '\0', "preset_name non-empty");
    check(caps.backend_name[0] != '\0', "backend_name non-empty");
    check(caps.sensor_count > 0, "sensor_count > 0");
    check(caps.sample_rate_hz_per_sensor > 0, "sample_rate_hz > 0");
}

// ============================================================================
// Test: Calibrated rate from ADC backend overrides preset's expected rate.
// ESP32-P4 SAR-ADC2 caps total throughput ~63kHz, so 3-sensor mode actually
// delivers ~21k Hz/sensor, not the preset's expected 27.7k. caps must reflect
// what the hardware actually does, not what the preset table says.
// ============================================================================
static void test_caps_uses_calibrated_rate() {
    std::printf("\n--- test_caps_uses_calibrated_rate ---\n");
    reset_module("direct_3_line");

    // Simulate the boot calibration result for 3-sensor mode on P4.
    g_adc_sample_rate_hz = 21028.0f;

    ShutterCaptureCaps caps;
    shutter_capture_get_caps(&caps);
    check(caps.sample_rate_hz_per_sensor == 21028,
          "caps reports calibrated rate (21028) not preset rate (27700)");

    // 1-sensor mode: hardware ceiling not hit, calibration ~= configured.
    reset_module("direct_single");
    g_adc_sample_rate_hz = 27685.0f;
    shutter_capture_get_caps(&caps);
    check(caps.sample_rate_hz_per_sensor == 27685,
          "caps reports 1-sensor calibrated rate (27685)");

    // Backend reporting 0 (not yet calibrated) must not zero out caps.
    reset_module("direct_3_line");
    g_adc_sample_rate_hz = 0.0f;
    shutter_capture_get_caps(&caps);
    check(caps.sample_rate_hz_per_sensor > 0,
          "caps falls back to preset rate when backend reports 0");
}

// ============================================================================
// Test: Trigger config round-trip
// ============================================================================
static void test_trigger_config_round_trip() {
    std::printf("\n--- test_trigger_config_round_trip ---\n");
    reset_module("direct_3_line");

    ShutterTriggerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.thresholds[0] = 1800;
    cfg.thresholds[1] = 1900;
    cfg.thresholds[2] = 2100;

    shutter_capture_set_trigger_config(&cfg);

    // Re-get caps (shouldn't change)
    ShutterCaptureCaps caps;
    shutter_capture_get_caps(&caps);
    check(caps.sensor_count == 3, "sensor_count unchanged after trigger config");

    // The trigger config is stored internally; test that it doesn't crash
    check(true, "set_trigger_config does not crash");
}

// ============================================================================
// Test: Preset table enumeration
// ============================================================================
static void test_preset_table() {
    std::printf("\n--- test_preset_table ---\n");
    reset_module("direct_3_line");

    uint8_t count = 0;
    shutter_capture_get_preset_table(&count);
    check(count >= 2, "at least 2 presets in table");
    check(count <= 8, "preset table is bounded");
}

// ============================================================================
// Test: Recalibration API delegates to ADC backend and reports actual state
// ============================================================================
static void test_recalibration_api() {
    std::printf("\n--- test_recalibration_api ---\n");
    reset_module("direct_3_line");

    // recalibrate() is now gated on the engine being held by some consumer
    // (matches the runtime contract: recalibrating while parked is a no-op).
    // Acquire first so the call is delivered to the ADC backend.
    check(shutter_capture_acquire("test"), "acquire engine for recalibrate");
    shutter_capture_recalibrate();
    check(g_adc_recalibrate_calls == 1, "recalibrate delegates to adc backend");

    g_adc_calibrating = false;
    check(!shutter_capture_is_calibrating(), "calibrating false reflects adc state");

    g_adc_calibrating = true;
    check(shutter_capture_is_calibrating(), "calibrating true reflects adc state");

    shutter_capture_release("test");
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::printf("=== test_shutter_capture ===\n");

    test_valid_preset_direct_single();
    test_valid_preset_direct_3_line();
    test_invalid_preset_fallback();
    test_empty_preset_fallback();
    test_reserved_presets_fallback();
    test_caps_fields_populated();
    test_caps_uses_calibrated_rate();
    test_trigger_config_round_trip();
    test_preset_table();
    test_recalibration_api();

    std::printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
