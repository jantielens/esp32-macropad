// ============================================================================
// Unit tests for shutter binding scheme
// ============================================================================
// Tests the [shutter:key] resolver via the registered binding template scheme.
// Covers:
//   1. Metadata keys: preset_id, preset_name, sensor_count, available
//   2. Null-safety: zeroed / null caps pointers return "---" without crashing
//   3. Dynamic per-sensor key generation and out-of-range placeholder behavior
//   4. Compatibility keys: sensor_1_ms..sensor_3_ms available in direct_3_line
//   5. Mode-aware spread: returns "---" for single-sensor, value for multi-sensor
//   6. History JSON: single-sensor entries omit "spread"; all entries include
//      "preset_id" and "sensor_count"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cmath>

// ---- Board and feature defines ----
#define HAS_DISPLAY         true
#define IS_SHUTTER_TESTER  true
#define SHUTTER_SENSOR_MAX  9
#define SHUTTER_HISTORY_SIZE 8

// ---- strlcpy (glibc < 2.38: declare here, stubs.cpp provides the definition) ----
#include <stddef.h>
#if !defined(__GLIBC__) || (__GLIBC__ < 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 38)
extern "C" size_t strlcpy(char* dst, const char* src, size_t siz);
#endif

// ---- PSRAM heap stubs (no PSRAM on host) ----
#define MALLOC_CAP_SPIRAM 0
inline void* heap_caps_malloc(size_t size, uint32_t) { return malloc(size); }
inline void  heap_caps_free(void* p)                 { free(p); }

// ---- Include headers for shared types ----
#include "../src/app/device_classes/shutter_tester/shutter_capture.h"
#include "../src/app/device_classes/shutter_tester/shutter_measure.h"
#include "../src/app/binding_template.h"

// ============================================================================
// Mock state — all tests control these globals
// ============================================================================

static ShutterCaptureCaps g_mock_caps;
static bool               g_mock_available = false;
static bool               g_mock_calibrating = false;
static ShutterMeasurement g_mock_latest;
static bool               g_mock_latest_valid = false;
static ShutterMeasurement g_mock_history[SHUTTER_HISTORY_SIZE];
static uint8_t            g_mock_history_count = 0;
static uint32_t           g_mock_measure_count = 0;

// ---- Mock shutter_capture ----

void shutter_capture_get_caps(ShutterCaptureCaps* out) {
    if (out) *out = g_mock_caps;
}
bool shutter_capture_is_available() { return g_mock_available; }
bool shutter_capture_is_calibrating() { return g_mock_calibrating; }
// Tests pre-date the refcounted lifecycle gate added in shutter_binding.cpp.
// Default to "running" so existing assertions that expect a resolved value
// (rather than the idle "---" placeholder) keep passing. Individual tests
// can flip g_mock_running false to exercise the idle contract.
static bool g_mock_running = true;
bool shutter_capture_is_running() { return g_mock_running; }

// ---- Mock shutter_measure ----

bool shutter_measure_get_latest(ShutterMeasurement* out) {
    if (!g_mock_latest_valid || !out) return false;
    *out = g_mock_latest;
    return true;
}
uint8_t shutter_measure_get_history(ShutterMeasurement* out, uint8_t max) {
    uint8_t n = g_mock_history_count < max ? g_mock_history_count : max;
    memcpy(out, g_mock_history, n * sizeof(ShutterMeasurement));
    return n;
}
uint32_t shutter_measure_get_count()  { return g_mock_measure_count; }
void shutter_measure_get_target(char* out_label, size_t len, bool* out_locked) {
    if (out_label && len > 0) out_label[0] = '\0';
    if (out_locked) *out_locked = false;
}

// Stub session API — controllable via test globals.
static bool     g_mock_session_active = false;
bool     shutter_session_is_active()  { return g_mock_session_active; }
uint32_t shutter_session_get_count()  { return 0; }
uint32_t shutter_session_get_id()     { return 0; }
void     shutter_session_get_type(char* out, size_t len) { if (out && len > 0) out[0] = '\0'; }
void     shutter_session_guide_get_target(char* out, size_t len) { if (out && len > 0) out[0] = '\0'; }
void     shutter_session_guide_get_step(char* out, size_t len) { if (out && len > 0) out[0] = '\0'; }
void     shutter_session_guide_get_steps(char* out, size_t len) { if (out && len > 0) out[0] = '\0'; }
void     shutter_session_guide_get_shot(char* out, size_t len) { if (out && len > 0) out[0] = '\0'; }
void     shutter_session_guide_get_shots(char* out, size_t len) { if (out && len > 0) out[0] = '\0'; }
void     shutter_session_guide_get_taking(char* out, size_t len) { if (out && len > 0) out[0] = '\0'; }
void     shutter_session_guide_get_total(char* out, size_t len) { if (out && len > 0) out[0] = '\0'; }
void     shutter_session_guide_get_name(char* out, size_t len) { if (out && len > 0) out[0] = '\0'; }
void     shutter_session_guide_get_id(char* out, size_t len) { if (out && len > 0) out[0] = '\0'; }

// Stub alignment API — controllable via test globals.
static bool g_mock_alignment_active = false;
static ShutterAlignmentReading g_mock_alignment_reading;
static bool g_mock_alignment_has_data = false;

bool shutter_capture_is_alignment_active() { return g_mock_alignment_active; }
bool shutter_capture_get_alignment(ShutterAlignmentReading* out) {
    if (!g_mock_alignment_active || !g_mock_alignment_has_data || !out) return false;
    *out = g_mock_alignment_reading;
    return true;
}

// ---- Include implementations under test ----
#include "../src/app/device_classes/shutter_tester/shutter_align_binding.cpp"
// shutter_align_binding.cpp defines no TAG; safe to include before shutter_binding.
#include "../src/app/device_classes/shutter_tester/shutter_binding.cpp"
// binding_template.cpp defines its own TAG; undef shutter_binding's TAG first.
#undef TAG
#include "../src/app/binding_template.cpp"

// ============================================================================
// Test harness
// ============================================================================

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

// Resolve a [shutter:key] token; returns the resolved string.
// Uses resolve_single_token so the full output buffer is passed directly to
// the resolver — important for history_json which can exceed the 128-byte
// intermediate buffer used by the multi-token resolve path.
static void resolve(const char* key, char* out, size_t out_len) {
    char templ[64];
    snprintf(templ, sizeof(templ), "[shutter:%s]", key);
    binding_template_resolve_single_token(templ, out, out_len);
}

// ---- Helpers to configure mock state ----

static void set_caps_3line() {
    memset(&g_mock_caps, 0, sizeof(g_mock_caps));
    g_mock_caps.sensor_count            = 3;
    g_mock_caps.sample_rate_hz_per_sensor = 27700;
    g_mock_caps.topology                = ShutterTopologyType::ThreeLine;
    g_mock_caps.preset_id               = ShutterPresetId::Direct3Line;
    g_mock_caps.preset_id_str           = "direct_3_line";
    g_mock_caps.preset_name             = "3-Line Direct";
    g_mock_caps.backend_name            = "adc_p4";
    g_mock_caps.waveform_available      = true;
    g_mock_caps.local_capture           = true;
    g_mock_available                    = true;
}

static void set_caps_single() {
    memset(&g_mock_caps, 0, sizeof(g_mock_caps));
    g_mock_caps.sensor_count            = 1;
    g_mock_caps.sample_rate_hz_per_sensor = 27700;
    g_mock_caps.topology                = ShutterTopologyType::SingleSensor;
    g_mock_caps.preset_id               = ShutterPresetId::DirectSingle;
    g_mock_caps.preset_id_str           = "direct_single";
    g_mock_caps.preset_name             = "Single Sensor";
    g_mock_caps.backend_name            = "adc_p4";
    g_mock_caps.waveform_available      = true;
    g_mock_caps.local_capture           = true;
    g_mock_available                    = true;
}

static void set_measurement_3line(float s1_ms, float s2_ms, float s3_ms) {
    memset(&g_mock_latest, 0, sizeof(g_mock_latest));
    g_mock_latest.sensor_count        = 3;
    g_mock_latest.valid_sensor_count  = 3;
    g_mock_latest.preset_id           = ShutterPresetId::Direct3Line;
    g_mock_latest.valid               = true;
    g_mock_latest.avg_duration_ms     = (s1_ms + s2_ms + s3_ms) / 3.0f;
    float mn = fminf(fminf(s1_ms, s2_ms), s3_ms);
    float mx = fmaxf(fmaxf(s1_ms, s2_ms), s3_ms);
    float avg = g_mock_latest.avg_duration_ms;
    g_mock_latest.spread_ms           = mx - mn;
    g_mock_latest.spread_pct          = avg > 0 ? (mx - mn) / avg * 100.0f : 0.0f;
    g_mock_latest.capping_gradient_stops_per_mm = -1.0f;
    g_mock_latest.deviation_pct       = 0.0f;
    g_mock_latest.deviation_stops     = 0.0f;
    g_mock_latest.nearest_duration_ms = avg;
    strlcpy(g_mock_latest.nearest_speed, "1/125s", sizeof(g_mock_latest.nearest_speed));
    g_mock_latest.verdict             = SHUTTER_VERDICT_PASS;
    g_mock_latest.sensors[0].valid    = true; g_mock_latest.sensors[0].duration_ms = s1_ms;
    g_mock_latest.sensors[1].valid    = true; g_mock_latest.sensors[1].duration_ms = s2_ms;
    g_mock_latest.sensors[2].valid    = true; g_mock_latest.sensors[2].duration_ms = s3_ms;
    g_mock_latest_valid               = true;
}

static void set_measurement_single(float s1_ms) {
    memset(&g_mock_latest, 0, sizeof(g_mock_latest));
    g_mock_latest.sensor_count        = 1;
    g_mock_latest.valid_sensor_count  = 1;
    g_mock_latest.preset_id           = ShutterPresetId::DirectSingle;
    g_mock_latest.valid               = true;
    g_mock_latest.avg_duration_ms     = s1_ms;
    g_mock_latest.spread_ms           = 0.0f;
    g_mock_latest.spread_pct          = 0.0f;
    g_mock_latest.capping_gradient_stops_per_mm = -1.0f;
    g_mock_latest.deviation_pct       = -2.0f;
    g_mock_latest.nearest_duration_ms = s1_ms;
    strlcpy(g_mock_latest.nearest_speed, "1/1000s", sizeof(g_mock_latest.nearest_speed));
    g_mock_latest.verdict             = SHUTTER_VERDICT_PASS;
    g_mock_latest.sensors[0].valid    = true; g_mock_latest.sensors[0].duration_ms = s1_ms;
    g_mock_latest_valid               = true;
}

// Reset the history-JSON cache between tests (count mismatch forces a rebuild).
static void reset_history_cache() {
    g_mock_measure_count++;
}

// ============================================================================
// Test: metadata keys resolve correctly for direct_3_line
// ============================================================================
static void test_metadata_keys_3line() {
    std::printf("\n--- test_metadata_keys_3line ---\n");
    set_caps_3line();
    char out[64];

    resolve("preset_id", out, sizeof(out));
    check(strcmp(out, "direct_3_line") == 0, "preset_id = direct_3_line");

    resolve("preset_name", out, sizeof(out));
    check(strcmp(out, "3-Line Direct") == 0, "preset_name = 3-Line Direct");

    resolve("sensor_count", out, sizeof(out));
    check(strcmp(out, "3") == 0, "sensor_count = 3");

    resolve("available", out, sizeof(out));
    check(strcmp(out, "true") == 0, "available = true");
}

// ============================================================================
// Test: metadata keys resolve correctly for direct_single
// ============================================================================
static void test_metadata_keys_single() {
    std::printf("\n--- test_metadata_keys_single ---\n");
    set_caps_single();
    char out[64];

    resolve("preset_id", out, sizeof(out));
    check(strcmp(out, "direct_single") == 0, "preset_id = direct_single");

    resolve("preset_name", out, sizeof(out));
    check(strcmp(out, "Single Sensor") == 0, "preset_name = Single Sensor");

    resolve("sensor_count", out, sizeof(out));
    check(strcmp(out, "1") == 0, "sensor_count = 1");
}

// ============================================================================
// Test: null/zeroed caps do not crash and return "---"
// ============================================================================
static void test_null_caps_safety() {
    std::printf("\n--- test_null_caps_safety ---\n");
    // Zeroed caps: preset_id_str and preset_name are nullptr.
    memset(&g_mock_caps, 0, sizeof(g_mock_caps));
    g_mock_available   = false;
    g_mock_latest_valid = false;
    char out[64];

    resolve("preset_id", out, sizeof(out));
    check(strcmp(out, "---") == 0, "null preset_id_str -> ---");

    resolve("preset_name", out, sizeof(out));
    check(strcmp(out, "---") == 0, "null preset_name -> ---");

    resolve("available", out, sizeof(out));
    check(strcmp(out, "false") == 0, "available = false when zeroed");
}

// ============================================================================
// Test: spread returns "---" in single-sensor mode, value in 3-line mode
// ============================================================================
static void test_spread_mode_awareness() {
    std::printf("\n--- test_spread_mode_awareness ---\n");
    char out[64];

    // Single-sensor: spread must be "---" even when measurement is valid.
    set_caps_single();
    set_measurement_single(1.2f);
    resolve("spread", out, sizeof(out));
    check(strcmp(out, "---") == 0, "spread = --- in single-sensor mode");

    resolve("spread_ms", out, sizeof(out));
    check(strcmp(out, "---") == 0, "spread_ms = --- in single-sensor mode");

    // 3-line: spread must return a numeric value.
    set_caps_3line();
    set_measurement_3line(8.0f, 8.5f, 8.2f);
    resolve("spread", out, sizeof(out));
    // Result is a formatted float, not "---".
    check(strcmp(out, "---") != 0, "spread != --- in 3-line mode");
    check(strlen(out) > 0, "spread non-empty in 3-line mode");
}

// ============================================================================
// Test: dynamic per-sensor keys, count-driven, with "---" beyond active count
// ============================================================================
static void test_dynamic_sensor_keys_3line() {
    std::printf("\n--- test_dynamic_sensor_keys_3line ---\n");
    set_caps_3line();
    set_measurement_3line(8.0f, 8.5f, 8.2f);
    char out[64];

    resolve("sensor_1_ms", out, sizeof(out));
    check(strcmp(out, "---") != 0, "sensor_1_ms present in 3-line");

    resolve("sensor_2_ms", out, sizeof(out));
    check(strcmp(out, "---") != 0, "sensor_2_ms present in 3-line");

    resolve("sensor_3_ms", out, sizeof(out));
    check(strcmp(out, "---") != 0, "sensor_3_ms present in 3-line");

    // Beyond the active sensor count → placeholder.
    resolve("sensor_4_ms", out, sizeof(out));
    check(strcmp(out, "---") == 0, "sensor_4_ms = --- in 3-line (beyond count)");

    resolve("sensor_9_ms", out, sizeof(out));
    check(strcmp(out, "---") == 0, "sensor_9_ms = --- in 3-line");
}

static void test_dynamic_sensor_keys_single() {
    std::printf("\n--- test_dynamic_sensor_keys_single ---\n");
    set_caps_single();
    set_measurement_single(1.2f);
    char out[64];

    resolve("sensor_1_ms", out, sizeof(out));
    check(strcmp(out, "---") != 0, "sensor_1_ms present in single-sensor");

    // Only 1 active sensor — sensor_2 and beyond are placeholders.
    resolve("sensor_2_ms", out, sizeof(out));
    check(strcmp(out, "---") == 0, "sensor_2_ms = --- in single-sensor mode");

    resolve("sensor_3_ms", out, sizeof(out));
    check(strcmp(out, "---") == 0, "sensor_3_ms = --- in single-sensor mode");
}

// ============================================================================
// Test: sensor_N_valid follows the same count-driven placeholder logic
// ============================================================================
static void test_sensor_valid_keys() {
    std::printf("\n--- test_sensor_valid_keys ---\n");
    set_caps_3line();
    set_measurement_3line(8.0f, 8.5f, 8.2f);
    char out[64];

    resolve("sensor_1_valid", out, sizeof(out));
    check(strcmp(out, "true") == 0, "sensor_1_valid = true");

    resolve("sensor_4_valid", out, sizeof(out));
    check(strcmp(out, "---") == 0, "sensor_4_valid = --- (beyond count)");

    set_caps_single();
    set_measurement_single(1.2f);

    resolve("sensor_2_valid", out, sizeof(out));
    check(strcmp(out, "---") == 0, "sensor_2_valid = --- in single-sensor");
}

// ============================================================================
// Test: history_json — single-sensor entries omit spread, include preset metadata
// ============================================================================
static void test_history_json_single_sensor() {
    std::printf("\n--- test_history_json_single_sensor ---\n");
    set_caps_single();
    g_mock_history_count = 0;

    // Build one single-sensor history entry.
    memset(&g_mock_history[0], 0, sizeof(g_mock_history[0]));
    g_mock_history[0].sensor_count       = 1;
    g_mock_history[0].valid_sensor_count = 1;
    g_mock_history[0].preset_id          = ShutterPresetId::DirectSingle;
    g_mock_history[0].valid              = true;
    g_mock_history[0].avg_duration_ms    = 1.0f;
    g_mock_history[0].spread_pct         = 0.0f;
    g_mock_history[0].deviation_pct      = -2.0f;
    g_mock_history[0].verdict            = SHUTTER_VERDICT_PASS;
    strlcpy(g_mock_history[0].nearest_speed, "1/1000s", sizeof(g_mock_history[0].nearest_speed));
    g_mock_history_count = 1;
    reset_history_cache();

    char out[512];
    resolve("history_json", out, sizeof(out));

    // Must be non-empty JSON array.
    check(out[0] == '[', "history_json starts with [");
    check(strstr(out, "1/1000s") != nullptr, "speed field present");

    // Metadata fields must be present.
    check(strstr(out, "\"preset_id\"") != nullptr, "preset_id field present");
    check(strstr(out, "direct_single") != nullptr, "preset_id value = direct_single");
    check(strstr(out, "\"sensor_count\"") != nullptr, "sensor_count field present");
    check(strstr(out, "\"sensor_count\":1") != nullptr, "sensor_count value = 1");

    // Spread must be absent for single-sensor captures.
    check(strstr(out, "\"spread\"") == nullptr, "spread field absent for single-sensor");
}

// ============================================================================
// Test: history_json — 3-line entries include spread, preset_id, sensor_count
// ============================================================================
static void test_history_json_3line() {
    std::printf("\n--- test_history_json_3line ---\n");
    set_caps_3line();
    g_mock_history_count = 0;

    memset(&g_mock_history[0], 0, sizeof(g_mock_history[0]));
    g_mock_history[0].sensor_count       = 3;
    g_mock_history[0].valid_sensor_count = 3;
    g_mock_history[0].preset_id          = ShutterPresetId::Direct3Line;
    g_mock_history[0].valid              = true;
    g_mock_history[0].avg_duration_ms    = 8.0f;
    g_mock_history[0].spread_pct         = 3.5f;
    g_mock_history[0].deviation_pct      = 0.5f;
    g_mock_history[0].verdict            = SHUTTER_VERDICT_PASS;
    strlcpy(g_mock_history[0].nearest_speed, "1/125s", sizeof(g_mock_history[0].nearest_speed));
    g_mock_history_count = 1;
    reset_history_cache();

    char out[512];
    resolve("history_json", out, sizeof(out));

    check(out[0] == '[', "history_json starts with [");
    check(strstr(out, "1/125s") != nullptr, "speed field present");

    // Spread must be present for 3-line captures.
    check(strstr(out, "\"spread\"") != nullptr, "spread field present for 3-line");

    // Metadata fields.
    check(strstr(out, "\"preset_id\"") != nullptr, "preset_id field present");
    check(strstr(out, "direct_3_line") != nullptr, "preset_id value = direct_3_line");
    check(strstr(out, "\"sensor_count\":3") != nullptr, "sensor_count value = 3");
}

// ============================================================================
// Test: history_json — mixed entries: first single-sensor, then 3-line
// ============================================================================
static void test_history_json_mixed() {
    std::printf("\n--- test_history_json_mixed ---\n");
    g_mock_history_count = 0;

    // Entry 0: single-sensor (spread must be absent)
    memset(&g_mock_history[0], 0, sizeof(g_mock_history[0]));
    g_mock_history[0].sensor_count       = 1;
    g_mock_history[0].preset_id          = ShutterPresetId::DirectSingle;
    g_mock_history[0].valid              = true;
    g_mock_history[0].avg_duration_ms    = 1.0f;
    g_mock_history[0].spread_pct         = 0.0f;
    g_mock_history[0].deviation_pct      = 0.0f;
    g_mock_history[0].verdict            = SHUTTER_VERDICT_PASS;
    strlcpy(g_mock_history[0].nearest_speed, "1/1000s", sizeof(g_mock_history[0].nearest_speed));

    // Entry 1: 3-line (spread must be present)
    memset(&g_mock_history[1], 0, sizeof(g_mock_history[1]));
    g_mock_history[1].sensor_count       = 3;
    g_mock_history[1].preset_id          = ShutterPresetId::Direct3Line;
    g_mock_history[1].valid              = true;
    g_mock_history[1].avg_duration_ms    = 8.0f;
    g_mock_history[1].spread_pct         = 2.0f;
    g_mock_history[1].deviation_pct      = 0.0f;
    g_mock_history[1].verdict            = SHUTTER_VERDICT_PASS;
    strlcpy(g_mock_history[1].nearest_speed, "1/125s", sizeof(g_mock_history[1].nearest_speed));

    g_mock_history_count = 2;
    reset_history_cache();

    char out[512];
    resolve("history_json", out, sizeof(out));

    // Both entries present.
    check(strstr(out, "1/1000s") != nullptr, "entry 0 speed present");
    check(strstr(out, "1/125s") != nullptr,  "entry 1 speed present");

    // Both preset_id values present.
    check(strstr(out, "direct_single") != nullptr, "entry 0 preset_id present");
    check(strstr(out, "direct_3_line") != nullptr, "entry 1 preset_id present");

    // At least one spread field exists (from entry 1).
    check(strstr(out, "\"spread\"") != nullptr, "spread present for 3-line entry");
}

// ============================================================================
// Test: compatibility — sensor_1_ms / sensor_2_ms / sensor_3_ms names unchanged
// ============================================================================
static void test_compat_keys_3line() {
    std::printf("\n--- test_compat_keys_3line ---\n");
    set_caps_3line();
    set_measurement_3line(7.8f, 8.1f, 8.3f);
    char out[64];

    // Existing pad configs reference these exact key names.
    resolve("sensor_1_ms", out, sizeof(out));
    check(strcmp(out, "---") != 0 && strlen(out) > 0, "sensor_1_ms resolves in direct_3_line");

    resolve("sensor_2_ms", out, sizeof(out));
    check(strcmp(out, "---") != 0 && strlen(out) > 0, "sensor_2_ms resolves in direct_3_line");

    resolve("sensor_3_ms", out, sizeof(out));
    check(strcmp(out, "---") != 0 && strlen(out) > 0, "sensor_3_ms resolves in direct_3_line");
}

// ============================================================================
// Test: no-data placeholders when capture is unavailable
// ============================================================================
static void test_no_data_placeholders() {
    std::printf("\n--- test_no_data_placeholders ---\n");
    set_caps_3line();
    g_mock_latest_valid = false;
    char out[64];

    // Keys that require a valid measurement return "---".
    resolve("speed", out, sizeof(out));
    check(strcmp(out, "---") == 0, "speed = --- when no measurement");

    resolve("duration_ms", out, sizeof(out));
    check(strcmp(out, "---") == 0, "duration_ms = --- when no measurement");

    resolve("sensor_1_ms", out, sizeof(out));
    check(strcmp(out, "---") == 0, "sensor_1_ms = --- when no measurement");

    resolve("spread", out, sizeof(out));
    check(strcmp(out, "---") == 0, "spread = --- when no measurement");
}

// ============================================================================
// Test: session keys use dot-prefix convention
// ============================================================================
static void test_session_keys() {
    std::printf("\n--- test_session_keys ---\n");
    set_caps_3line();
    char out[64];

    resolve("session.active", out, sizeof(out));
    check(strcmp(out, "false") == 0, "session.active = false (stub)");

    resolve("session.count", out, sizeof(out));
    check(strcmp(out, "0") == 0, "session.count = 0 (stub)");

    resolve("session.id", out, sizeof(out));
    check(strcmp(out, "") == 0, "session.id = empty (stub, id=0)");
}

// ============================================================================
// Test: calibration keys use dot-prefix convention and numeric active flag
// ============================================================================
static void test_calibration_keys() {
    std::printf("\n--- test_calibration_keys ---\n");
    set_caps_3line();
    char out[64];

    g_mock_calibrating = false;
    resolve("calib.active", out, sizeof(out));
    check(strcmp(out, "0") == 0, "calib.active = 0 when inactive");

    g_mock_calibrating = true;
    resolve("calib.active", out, sizeof(out));
    check(strcmp(out, "1") == 0, "calib.active = 1 when active");

    g_mock_calibrating = false;
}

// ============================================================================
// Test: alignment binding keys when inactive
// ============================================================================
static void test_alignment_inactive() {
    std::printf("\n--- test_alignment_inactive ---\n");
    g_mock_alignment_active = false;
    g_mock_alignment_has_data = false;
    char out[64];

    resolve("align.active", out, sizeof(out));
    check(strcmp(out, "false") == 0, "align.active = false when inactive");

    resolve("align.s1_pct", out, sizeof(out));
    check(strcmp(out, "---") == 0, "align.s1_pct = --- when inactive");

    resolve("align.spread", out, sizeof(out));
    check(strcmp(out, "---") == 0, "align.spread = --- when inactive");

    resolve("align.status", out, sizeof(out));
    check(strcmp(out, "---") == 0, "align.status = --- when inactive");

    resolve("align.hint", out, sizeof(out));
    check(strcmp(out, "---") == 0, "align.hint = --- when inactive");
}

// ============================================================================
// Test: alignment binding keys when active with data
// ============================================================================
static void test_alignment_active() {
    std::printf("\n--- test_alignment_active ---\n");
    g_mock_alignment_active = true;
    g_mock_alignment_has_data = true;
    memset(&g_mock_alignment_reading, 0, sizeof(g_mock_alignment_reading));
    g_mock_alignment_reading.sensor_count = 3;
    g_mock_alignment_reading.pct[0] = 85;
    g_mock_alignment_reading.pct[1] = 87;
    g_mock_alignment_reading.pct[2] = 83;
    g_mock_alignment_reading.raw[0] = 500;
    g_mock_alignment_reading.raw[1] = 430;
    g_mock_alignment_reading.raw[2] = 570;
    g_mock_alignment_reading.spread_pct = 4;
    g_mock_alignment_reading.status = "ready";
    g_mock_alignment_reading.hint = "";
    g_mock_alignment_reading.valid = true;
    char out[64];

    resolve("align.active", out, sizeof(out));
    check(strcmp(out, "true") == 0, "align.active = true when active");

    resolve("align.s1_pct", out, sizeof(out));
    check(strcmp(out, "85") == 0, "align.s1_pct = 85");

    resolve("align.s2_pct", out, sizeof(out));
    check(strcmp(out, "87") == 0, "align.s2_pct = 87");

    resolve("align.s3_pct", out, sizeof(out));
    check(strcmp(out, "83") == 0, "align.s3_pct = 83");

    resolve("align.s1_raw", out, sizeof(out));
    check(strcmp(out, "500") == 0, "align.s1_raw = 500");

    resolve("align.spread", out, sizeof(out));
    check(strcmp(out, "4") == 0, "align.spread = 4");

    resolve("align.status", out, sizeof(out));
    check(strcmp(out, "ready") == 0, "align.status = ready");

    resolve("align.hint", out, sizeof(out));
    check(strcmp(out, "") == 0, "align.hint = empty when ready");

    resolve("align.sensor_count", out, sizeof(out));
    check(strcmp(out, "3") == 0, "align.sensor_count = 3");

    // Beyond active count.
    resolve("align.s4_pct", out, sizeof(out));
    check(strcmp(out, "---") == 0, "align.s4_pct = --- (beyond count)");

    // Clean up.
    g_mock_alignment_active = false;
    g_mock_alignment_has_data = false;
}

// ============================================================================
// Test: capping_gradient binding
// ============================================================================
static void test_capping_gradient_binding() {
    std::printf("\n--- test_capping_gradient_binding ---\n");
    char out[128];

    // With gradient not computed (sentinel -1): should resolve to empty string
    set_measurement_3line(8.0f, 8.2f, 8.4f);
    g_mock_latest.capping_gradient_stops_per_mm = -1.0f;
    resolve("capping_gradient", out, sizeof(out));
    check(out[0] == '\0', "capping_gradient = empty when not computed");

    // With gradient computed: should resolve to 3-decimal value
    g_mock_latest.capping_gradient_stops_per_mm = 0.012f;
    resolve("capping_gradient", out, sizeof(out));
    check(strcmp(out, "0.012") == 0, "capping_gradient = 0.012 when set");

    // With very small gradient (< 0.001): empty string, same as portal JS
    g_mock_latest.capping_gradient_stops_per_mm = 0.0001f;
    resolve("capping_gradient", out, sizeof(out));
    check(out[0] == '\0', "capping_gradient = empty for tiny value < 0.001");

    // capping_frame_stops: gradient * 43.27 (35mm diagonal)
    g_mock_latest.capping_gradient_stops_per_mm = 0.012f;
    resolve("capping_frame_stops", out, sizeof(out));
    check(strcmp(out, "0.52") == 0, "capping_frame_stops = 0.52 for gradient 0.012");

    // capping_frame_stops: empty when gradient unavailable
    g_mock_latest.capping_gradient_stops_per_mm = -1.0f;
    resolve("capping_frame_stops", out, sizeof(out));
    check(out[0] == '\0', "capping_frame_stops = empty when not computed");

    // capping_frame_stops: empty for negligible gradient
    g_mock_latest.capping_gradient_stops_per_mm = 0.0005f;
    resolve("capping_frame_stops", out, sizeof(out));
    check(out[0] == '\0', "capping_frame_stops = empty for tiny gradient");
}

// ============================================================================
// main
// ============================================================================

int main() {
    // Initialise the binding engine and register the shutter scheme.
    shutter_binding_init();

    test_metadata_keys_3line();
    test_metadata_keys_single();
    test_null_caps_safety();
    test_spread_mode_awareness();
    test_dynamic_sensor_keys_3line();
    test_dynamic_sensor_keys_single();
    test_sensor_valid_keys();
    test_history_json_single_sensor();
    test_history_json_3line();
    test_history_json_mixed();
    test_compat_keys_3line();
    test_no_data_placeholders();
    test_session_keys();
    test_calibration_keys();
    test_alignment_inactive();
    test_alignment_active();
    test_capping_gradient_binding();

    std::printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
