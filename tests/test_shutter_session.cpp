// ============================================================================
// Unit tests for the shutter session state machine.
// ============================================================================
//
// Drives shutter_session.cpp through its public API and observes the side
// effects produced on the shutter_measure HAL (set_target / set_lock) and on
// the session's own state flags. These tests encode the lock-release contract
// that was broken when the bug "freeform session stuck on last guided speed"
// was reported.
//
// All heavy collaborators (FsIndexedStore, Storage, FreeRTOS, capture,
// measure, test scripts) are stubbed below so the focus stays on state
// transitions, not persistence.
// ============================================================================

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>

// Pull in headers that session.cpp depends on so we can define the symbols
// it expects (matching the real signatures).
#include "shutter_capture.h"
#include "shutter_measure.h"
#include "shutter_test_scripts.h"

// ============================================================================
// Global stub instances
// ============================================================================

#include <LittleFS.h>
FakeStorage LittleFS;  // satisfies `Storage` macro from src/app/storage.h

void storage_publish_usage(bool /*force*/) {}

// ============================================================================
// shutter_measure stubs — record calls so tests can assert side effects.
// ============================================================================

struct MeasureRecorder {
    int  set_target_calls     = 0;
    int  set_lock_true_calls  = 0;
    int  set_lock_false_calls = 0;
    char last_target[32]      = {};
    bool last_recompute       = true;
    void reset() { *this = MeasureRecorder(); }
};
static MeasureRecorder g_measure;

bool shutter_measure_set_target(const char* label, bool recompute) {
    g_measure.set_target_calls++;
    g_measure.last_recompute = recompute;
    if (label) {
        strncpy(g_measure.last_target, label, sizeof(g_measure.last_target) - 1);
        g_measure.last_target[sizeof(g_measure.last_target) - 1] = '\0';
    } else {
        g_measure.last_target[0] = '\0';
    }
    return true;
}
bool shutter_measure_set_lock(bool locked) {
    if (locked) g_measure.set_lock_true_calls++;
    else        g_measure.set_lock_false_calls++;
    return true;
}
bool shutter_measure_get_latest(ShutterMeasurement* /*out*/) { return false; }
void shutter_measure_get_sensor_offsets(float* x, float* y, float* d) {
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    if (d) *d = 0.0f;
}
uint8_t shutter_measure_get_geometry(ShutterSensorPosition* /*out*/, uint8_t /*max*/) {
    return 0;
}

// ============================================================================
// shutter_capture stubs
// ============================================================================

void shutter_capture_get_caps(ShutterCaptureCaps* out) {
    if (!out) return;
    *out = ShutterCaptureCaps();
    out->sensor_count             = 3;
    out->sample_rate_hz_per_sensor = 27700;  // SHUTTER_SAMPLE_RATE_HZ
    out->topology                 = ShutterTopologyType::ThreeLine;
    out->preset_id                = ShutterPresetId::Direct3Line;
    out->preset_id_str            = "direct_3_line";
    out->preset_name              = "3-Line Direct";
    out->backend_name             = "test";
}
bool shutter_capture_get_latest(ShutterCaptureFrame* /*out*/) { return false; }
void shutter_capture_stop_alignment() {}

// Refcount API — record acquire/release pairing so tests can assert balance.
struct CaptureLifecycleRecorder {
    int  acquire_calls = 0;
    int  release_calls = 0;
    char last_acquire_tag[32] = {};
    char last_release_tag[32] = {};
    void reset() { *this = CaptureLifecycleRecorder(); }
};
static CaptureLifecycleRecorder g_capture_lifecycle;
bool shutter_capture_acquire(const char* tag) {
    g_capture_lifecycle.acquire_calls++;
    if (tag) { strncpy(g_capture_lifecycle.last_acquire_tag, tag, sizeof(g_capture_lifecycle.last_acquire_tag) - 1); }
    return true;
}
void shutter_capture_release(const char* tag) {
    g_capture_lifecycle.release_calls++;
    if (tag) { strncpy(g_capture_lifecycle.last_release_tag, tag, sizeof(g_capture_lifecycle.last_release_tag) - 1); }
}
bool shutter_capture_is_running() { return g_capture_lifecycle.acquire_calls > g_capture_lifecycle.release_calls; }

// ============================================================================
// shutter_test_scripts stubs — the test fixture installs a single script.
// ============================================================================

static ShutterTestParseResult g_test_result;

uint8_t shutter_test_scripts_parse(ShutterTestParseResult* result) {
    if (!result) return 0;
    *result = g_test_result;
    return g_test_result.count;
}
const ShutterTestScript* shutter_test_scripts_find(
        const ShutterTestParseResult* result, const char* id) {
    if (!result || !id) return nullptr;
    for (uint8_t i = 0; i < result->count; i++) {
        if (strcmp(result->scripts[i].id, id) == 0) return &result->scripts[i];
    }
    return nullptr;
}

static void install_test_script(const char* id, const char* name,
                                const char* const* speeds, uint16_t speed_count,
                                uint8_t shots_per) {
    g_test_result = ShutterTestParseResult();
    g_test_result.count = 1;
    ShutterTestScript& s = g_test_result.scripts[0];
    strncpy(s.id, id, sizeof(s.id) - 1);
    strncpy(s.name, name, sizeof(s.name) - 1);
    s.shots_per_speed = shots_per;
    s.speed_count = speed_count;
    for (uint16_t i = 0; i < speed_count && i < SHUTTER_TEST_MAX_SPEEDS; i++) {
        strncpy(s.speeds[i].speed, speeds[i], sizeof(s.speeds[i].speed) - 1);
        snprintf(s.speeds[i].speed_suffixed, sizeof(s.speeds[i].speed_suffixed),
                 "%ss", speeds[i]);
    }
}

// ============================================================================
// FsIndexedStore stubs — provide just-enough implementations so session.cpp
// can construct, init, and invoke persistence calls without real I/O.
// ============================================================================

#include "fs_indexed_store.h"

FsIndexedStore::FsIndexedStore(const char* base_path,
                               const char* const* index_fields,
                               size_t num_index_fields,
                               const FsIndexedStoreRootField* root_fields,
                               size_t num_root_fields)
    : _base_path(base_path),
      _index_fields(index_fields),
      _num_index_fields(num_index_fields),
      _root_fields(root_fields),
      _num_root_fields(num_root_fields),
      _mutex(nullptr),
      _manifest_doc(nullptr),
      _loaded(false) {}

bool FsIndexedStore::begin() { return true; }
String FsIndexedStore::data_path(const char* id) const {
    String p = "/test/";
    p += (id ? id : "");
    return p;
}
bool FsIndexedStore::register_pre_written(const char*, uint32_t, const JsonObject&) {
    return true;
}
bool FsIndexedStore::set_root_uint32(const char*, uint32_t) { return true; }
bool FsIndexedStore::get_root_uint32(const char*, uint32_t& out) {
    out = 1;
    return true;
}

// ============================================================================
// System under test
// ============================================================================

#include "shutter_session.h"

// ============================================================================
// Test harness
// ============================================================================

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, label) do {                              \
    if (cond) { printf("  PASS [%s]\n", label); g_pass++; }  \
    else      { printf("  FAIL [%s]\n", label); g_fail++; }  \
} while (0)

static ShutterMeasurement make_measurement(uint32_t cap_id) {
    ShutterMeasurement m;
    memset(&m, 0, sizeof(m));
    m.valid = true;
    m.sensor_count = 3;
    m.valid_sensor_count = 3;
    m.capture_id = cap_id;
    m.timestamp_ms = cap_id * 100;
    m.avg_duration_ms = 8.0f;
    m.nearest_duration_ms = 8.0f;
    strcpy(m.nearest_speed, "1/125s");
    m.verdict = SHUTTER_VERDICT_PASS;
    m.capping_gradient_stops_per_mm = -1.0f;
    m.capping_gradient_x_stops_per_mm = -1.0f;
    m.capping_gradient_y_stops_per_mm = -1.0f;
    m.skew_differential_us_per_mm = -1.0f;
    return m;
}

// ----------------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------------

static void test_freeform_stop_does_not_re_lock() {
    printf("\n--- test_freeform_stop_does_not_re_lock ---\n");
    shutter_session_start("camera_a");
    ShutterMeasurement m = make_measurement(1);
    shutter_session_on_measurement(&m);

    g_measure.reset();
    shutter_session_stop();

    CHECK(g_measure.set_lock_true_calls == 0,
          "freeform stop does not re-lock measurement");
}

static void test_guided_start_locks_and_targets_first_speed() {
    printf("\n--- test_guided_start_locks_and_targets_first_speed ---\n");
    const char* speeds[] = {"1/60", "1/125", "1/250"};
    install_test_script("t1", "Test 1", speeds, 3, /*shots_per=*/2);

    g_measure.reset();
    shutter_session_guide_start("t1");

    CHECK(shutter_session_is_active(), "session is active after guide_start");
    CHECK(shutter_session_is_guided(), "session is guided after guide_start");
    CHECK(g_measure.set_lock_true_calls == 1,
          "guide_start locks measurement");
    CHECK(g_measure.set_target_calls >= 1,
          "guide_start sets initial target");
    CHECK(strcmp(g_measure.last_target, "1/60s") == 0,
          "initial target equals first speed");

    shutter_session_stop();
}

static void test_guided_manual_stop_releases_lock() {
    printf("\n--- test_guided_manual_stop_releases_lock ---\n");
    const char* speeds[] = {"1/60", "1/125"};
    install_test_script("t2", "Test 2", speeds, 2, /*shots_per=*/3);
    shutter_session_guide_start("t2");

    ShutterMeasurement m = make_measurement(1);
    shutter_session_on_measurement(&m);

    g_measure.reset();
    shutter_session_stop();

    CHECK(g_measure.set_lock_false_calls >= 1,
          "manual stop of guided session releases lock");
    CHECK(!shutter_session_is_active(), "session is inactive after stop");
    CHECK(!shutter_session_is_guided(), "guided flag cleared after stop");
}

static void test_guided_auto_stop_releases_lock() {
    printf("\n--- test_guided_auto_stop_releases_lock ---\n");
    const char* speeds[] = {"1/250"};  // single speed
    install_test_script("t3", "Test 3", speeds, 1, /*shots_per=*/1);
    shutter_session_guide_start("t3");

    g_measure.reset();
    ShutterMeasurement m = make_measurement(1);
    shutter_session_on_measurement(&m);

    CHECK(!shutter_session_is_active(),
          "auto-stop deactivates session after last shot");
    CHECK(g_measure.set_lock_false_calls >= 1,
          "auto-stop releases measurement lock");
}

static void test_guided_skip_past_last_releases_lock() {
    printf("\n--- test_guided_skip_past_last_releases_lock ---\n");
    const char* speeds[] = {"1/30"};
    install_test_script("t4", "Test 4", speeds, 1, /*shots_per=*/5);
    shutter_session_guide_start("t4");

    g_measure.reset();
    shutter_session_guide_skip();

    CHECK(!shutter_session_is_active(),
          "skip past last speed deactivates session");
    CHECK(g_measure.set_lock_false_calls >= 1,
          "skip past last speed releases measurement lock");
}

static void test_freeform_after_guided_is_not_pre_locked() {
    printf("\n--- test_freeform_after_guided_is_not_pre_locked ---\n");
    // Reproduces the user-visible bug: after a guided session auto-completes,
    // a subsequent freeform session must start with a clean measurement-engine
    // state (no leftover lock, no leftover target).
    const char* speeds[] = {"1/500"};
    install_test_script("t5", "Bug repro", speeds, 1, /*shots_per=*/1);
    shutter_session_guide_start("t5");
    ShutterMeasurement m = make_measurement(1);
    shutter_session_on_measurement(&m);  // auto-stops

    // After the auto-stop, set_lock(false) must have been observed.
    CHECK(g_measure.set_lock_false_calls >= 1,
          "guided auto-stop released the lock before freeform start");

    g_measure.reset();
    shutter_session_start("freeform_camera");

    CHECK(g_measure.set_lock_true_calls == 0,
          "freeform start does not lock measurement");
    CHECK(g_measure.set_target_calls == 0,
          "freeform start does not override target");

    shutter_session_stop();
}

static void test_guide_redo_does_not_underflow_with_zero_shots_per() {
    printf("\n--- test_guide_redo_does_not_underflow_with_zero_shots_per ---\n");
    const char* speeds[] = {"1/60", "1/125"};
    install_test_script("t6", "Zero shots", speeds, 2, /*shots_per=*/0);
    shutter_session_guide_start("t6");

    // Redo must not move the shot index past 0 (i.e. not wrap to 65535).
    shutter_session_guide_redo();
    CHECK(shutter_session_is_active(),
          "redo with shots_per=0 does not crash or deactivate session");

    shutter_session_stop();
}

int main() {
    shutter_session_init();

    test_freeform_stop_does_not_re_lock();
    test_guided_start_locks_and_targets_first_speed();
    test_guided_manual_stop_releases_lock();
    test_guided_auto_stop_releases_lock();
    test_guided_skip_past_last_releases_lock();
    test_freeform_after_guided_is_not_pre_locked();
    test_guide_redo_does_not_underflow_with_zero_shots_per();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
