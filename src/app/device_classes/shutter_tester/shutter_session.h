#pragma once

#include "board_config.h"
#include "shutter_defaults.h"

#if IS_SHUTTER_TESTER

#include "shutter_measure.h"
#include "shutter_curtain_stats.h"

#include <stdint.h>
#include <stddef.h>

// Maximum number of waveform samples stored per sensor per measurement.
// Min-max downsampling is applied when raw sample count exceeds this limit.
#ifndef SHUTTER_WAVEFORM_STORE_POINTS
#define SHUTTER_WAVEFORM_STORE_POINTS 1000
#endif

// ============================================================================
// Snapshot types
// ============================================================================

// Per-sensor snapshot stored inside ShutterSessionMeasurement.
// Waveform data is heap_caps_malloc'd (PSRAM) and owned by this struct.
struct ShutterSessionSensor {
    float    duration_ms;
    uint16_t min_adc;
    uint16_t baseline_adc;
    uint16_t threshold;
    bool     valid;

    // Downsampled waveform (PSRAM-owned, may be null if waveform unavailable).
    uint16_t* waveform;      // heap_caps_malloc'd; free when session is destroyed
    uint32_t  waveform_len;

    // Positions as fractions [0..1] within the stored waveform slice.
    // Computed at snapshot time so JS can draw markers without knowing raw indices.
    float     trigger_frac;      // Where the first threshold crossing occurs
    float     pulse_start_frac;  // Leading edge (start_idx) within slice
    float     pulse_end_frac;    // Trailing edge (end_idx) within slice

    // Pre-computed curtain statistics from the full-resolution raw waveform.
    ShutterCurtainStats curtain_stats;
};

// Snapshot of a single measurement captured during a session.
// ShutterMeasurement fields are copied; waveforms are deep-copied into PSRAM.
struct ShutterSessionMeasurement {
    uint32_t timestamp_ms;
    char     nearest_speed[16];    // e.g. "1/125s" (comparison target label)
    float    nearest_duration_ms;  // Nominal duration of comparison target in ms
    float    avg_duration_ms;
    float    deviation_pct;
    float    deviation_stops;
    uint8_t  verdict;              // 0=pass, 1=warning, 2=fail
    uint8_t  sensor_count;
    uint8_t  valid_sensor_count;
    float    spread_pct;
    float    capping_gradient_stops_per_mm; // -1 if not computed
    float    capping_gradient_x_stops_per_mm; // Horizontal gradient (4-corner); -1 if not computed
    float    capping_gradient_y_stops_per_mm; // Vertical gradient (4-corner); -1 if not computed
    float    skew_differential_us_per_mm;     // Differential skew µs/mm (4-corner); -1 if not computed
    float    curtain1_skew_left_us;           // Curtain 1 skew at left sensors (µs); 0 if not computed
    float    curtain1_skew_right_us;          // Curtain 1 skew at right sensors (µs); 0 if not computed
    float    curtain2_skew_left_us;           // Curtain 2 skew at left sensors (µs); 0 if not computed
    float    curtain2_skew_right_us;          // Curtain 2 skew at right sensors (µs); 0 if not computed
    char     detected_travel[4];              // "V", "H", "L", or ""
    float    sample_rate_hz;       // ADC sample rate at capture time (per sensor)
    bool     target_manual;        // User manually set the target (not auto-detected)
    bool     speed_locked;         // Speed lock was active during this measurement
    char     target_speed[16];     // Guided target speed for this shot (empty if freeform)
    ShutterSessionSensor sensors[SHUTTER_SENSOR_MAX];
};

// ============================================================================
// Public API
// ============================================================================

// Call once from setup() inside IS_SHUTTER_TESTER block, after LittleFS is mounted.
void shutter_session_init();

// Start a new session. camera may be nullptr or empty.
// No-op if a session is already active.
void shutter_session_start(const char* camera);

// Stop the current session: persists it to LittleFS via FsIndexedStore, then clears state.
// No-op if no session is active.
void shutter_session_stop();

// Toggle: start if inactive, stop if active.
// camera is used only when starting; ignored when stopping.
void shutter_session_toggle(const char* camera);

// Remove the last measurement from the in-progress session.
// No-op if no session is active or session is empty.
void shutter_session_discard_last();

// Returns true while a session is open (between start and stop).
bool shutter_session_is_active();

// Returns the number of measurements in the current open session (0 if inactive).
uint32_t shutter_session_get_count();

// Returns the numeric ID that will be assigned to the current session when stopped,
// or 0 if no session is active. The stored ID string is "sess_N".
uint32_t shutter_session_get_id();

// ============================================================================
// Guided session API
// ============================================================================

// Start a guided session from a test script id.
// Parses the test file, finds the test, starts a session, locks first speed.
// No-op if a session is already active or test id not found.
void shutter_session_guide_start(const char* test_id);

// Stop a guided session (saves whatever measurements collected so far).
// Alias for shutter_session_stop().
void shutter_session_guide_stop();

// Skip the current speed in a guided session and advance to the next.
// If at the last speed, auto-stops the session.
// No-op if no guided session is active.
void shutter_session_guide_skip();

// Redo: discard the last measurement and decrement the shot counter.
// No-op if no guided session or no shots at current speed.
void shutter_session_guide_redo();

// Returns true if the active session is a guided session.
bool shutter_session_is_guided();

// Guided state getters for bindings. All return empty strings if not guided.
void shutter_session_guide_get_target(char* out, size_t len);
void shutter_session_guide_get_step(char* out, size_t len);
void shutter_session_guide_get_steps(char* out, size_t len);
void shutter_session_guide_get_shot(char* out, size_t len);
void shutter_session_guide_get_shots(char* out, size_t len);
void shutter_session_guide_get_taking(char* out, size_t len);
void shutter_session_guide_get_total(char* out, size_t len);
void shutter_session_guide_get_name(char* out, size_t len);
void shutter_session_guide_get_id(char* out, size_t len);
// Returns "freeform", "guided", or "" (no session).
void shutter_session_get_type(char* out, size_t len);

// Hook called by shutter_measure_process() after a new measurement is computed.
// Appends a snapshot to the active session buffer (PSRAM). No-op if inactive.
// Called from the measurement task — mutex-safe internally.
void shutter_session_on_measurement(const ShutterMeasurement* m);

// Hook called by recompute_target_fields() after s_latest is updated in-place.
// Updates the last snapshot in the session buffer to reflect the recomputed values.
// No-op if inactive or buffer is empty.
void shutter_session_on_recompute();

// Return the FsIndexedStore instance backing session persistence.
// Used by web_portal_shutter_sessions.cpp to register REST routes.
// Full definition is in fs_indexed_store.h; callers that use the return value
// must include that header themselves.
class FsIndexedStore;
FsIndexedStore& shutter_session_get_store();

#endif // IS_SHUTTER_TESTER
