#pragma once

#include "board_config.h"

#if IS_SHUTTER_TESTER

#include "shutter_capture.h"
#include <stdint.h>
#include <stddef.h>

// ============================================================================
// Shutter Measure — Shutter speed computation from captured waveforms
// ============================================================================
// Analyzes captured ADC waveforms to compute per-sensor exposure duration,
// maps to nearest standard shutter speed, and maintains rolling history.

// Maximum number of measurements in the rolling history.
#define SHUTTER_HISTORY_SIZE 8

// Minimum pulse depth (baseline - min_adc) required to accept a measurement.
// Rejects noise-triggered captures where the ADC barely dipped below threshold.
#define SHUTTER_MIN_PULSE_DEPTH       100

// Minimum per-sensor pulse duration to accept as valid (milliseconds).
// Rejects single-sample noise blips. 0.45 ms is below the fastest
// supported shutter speed (1/2000s = 0.50 ms nominal).
#ifndef SHUTTER_MIN_PULSE_DURATION_MS
#define SHUTTER_MIN_PULSE_DURATION_MS  0.45f
#endif

// Maximum fraction of capture buffer a valid pulse may span.
// Rejects ambient-drift captures where the signal never truly returns
// to baseline. 0.80 = 80%. A 1s exposure uses ~63% of max buffer.
#ifndef SHUTTER_MAX_PULSE_COVERAGE
#define SHUTTER_MAX_PULSE_COVERAGE     0.80f
#endif

// Maximum ratio between longest and shortest valid sensor durations.
// If exceeded, all sensors are invalidated (incoherent capture).
#ifndef SHUTTER_MAX_SENSOR_RATIO
#define SHUTTER_MAX_SENSOR_RATIO       4.0f
#endif

// Number of CSV columns emitted per sensor in the [MEAS] serial stream.
// Must match CSV_SENSOR_COL_COUNT in monitor_meas.sh.
#define SHUTTER_SENSOR_CSV_COL_COUNT  10

// Verdict values.
enum ShutterVerdict : uint8_t {
    SHUTTER_VERDICT_PASS    = 0,
    SHUTTER_VERDICT_WARNING = 1,
    SHUTTER_VERDICT_FAIL    = 2,
};

// Per-sensor measurement result.
struct ShutterSensorResult {
    float    duration_ms;    // Exposure duration in milliseconds
    uint16_t min_adc;        // Minimum ADC value during pulse (peak light)
    uint16_t baseline_adc;   // Average ADC before pulse (idle level for this sensor)
    uint16_t idle_noise_rms; // RMS noise of last ~200 idle samples before pulse
    uint16_t threshold;      // ADC threshold active during this capture
    uint32_t start_idx;      // First sample below threshold
    uint32_t end_idx;        // Last sample below threshold
    uint32_t total_samples;  // Total samples in the waveform
    bool     valid;          // True if threshold crossing was found
};

// Single measurement result.
struct ShutterMeasurement {
    ShutterSensorResult sensors[SHUTTER_SENSOR_MAX]; // per-sensor results; 0..sensor_count-1 valid
    uint8_t  sensor_count;         // Active sensors when this measurement was taken
    uint8_t  valid_sensor_count;   // Number of sensors with valid results
    ShutterPresetId preset_id;     // Preset active when this measurement was taken
    uint32_t capture_id;           // capture_id from the frame this measurement came from
    float    avg_duration_ms;     // Average across valid sensors
    float    spread_pct;          // Spread: (max-min)/avg × 100 (0 if valid_sensor_count < 2)
    float    spread_ms;           // Absolute spread: max-min in ms (0 if valid_sensor_count < 2)
    float    capping_gradient_stops_per_mm; // Capping gradient (stops/mm along sensor diagonal); -1 = not computed
    float    capping_gradient_x_stops_per_mm; // Horizontal gradient (4-corner); -1 = not computed
    float    capping_gradient_y_stops_per_mm; // Vertical gradient (4-corner); -1 = not computed
    float    skew_differential_us_per_mm;     // Differential skew between curtain rows (µs/mm); -1 = not computed
    float    curtain1_skew_left_us;           // Curtain 1 skew at left sensors (BL-TL timing, µs); 0 = not computed
    float    curtain1_skew_right_us;          // Curtain 1 skew at right sensors (BR-TR timing, µs); 0 = not computed
    float    curtain2_skew_left_us;           // Curtain 2 skew at left sensors (BL-TL timing, µs); 0 = not computed
    float    curtain2_skew_right_us;          // Curtain 2 skew at right sensors (BR-TR timing, µs); 0 = not computed
    char     detected_travel[4];              // Auto-detected travel: "V", "H", "L", or "" (empty)
    float    deviation_pct;       // Deviation from comparison target (%)
    float    deviation_stops;     // Deviation in stops (log2 ratio)
    char     nearest_speed[16];   // Comparison target label e.g. "1/125s"
    float    nearest_duration_ms; // Comparison target nominal duration
    bool     target_manual;       // True if target was set manually
    bool     speed_locked;        // True if lock is active
    ShutterVerdict verdict;       // Pass / warning / fail
    uint32_t timestamp_ms;        // millis() when measured
    bool     valid;               // True if at least one sensor produced a result
};

// Initialize the measurement engine. Call once from setup().
void shutter_measure_init();

// Set per-sensor positions from the active preset's geometry.
// Precomputes the legacy diagonal_mm for backward-compat capping gradient.
// Call after shutter_measure_init() with positions from the resolved preset.
void shutter_measure_set_geometry(const ShutterSensorPosition* positions, uint8_t count);

// Get the per-sensor position array. Returns count (0 if not set).
// If out is non-null and max_count >= count, copies positions into out.
uint8_t shutter_measure_get_geometry(ShutterSensorPosition* out, uint8_t max_count);

// Get the current sensor offset values and precomputed diagonal.
// Any output pointer may be null.
void shutter_measure_get_sensor_offsets(float* out_x, float* out_y, float* out_diagonal);

// Process a capture frame into a measurement result.
// Stateless computation: fills *out without touching module state.
// Also called by shutter_measure_process() after getting a new frame,
// and directly from unit tests with synthetic frames.
void shutter_measure_process_capture(const ShutterCaptureFrame* frame, ShutterMeasurement* out);

// Poll the capture seam for new frames and compute measurements.
// Called from the app loop. Stores result for later retrieval.
void shutter_measure_process();

// Get the most recent measurement (safe to call from LVGL task).
// Returns false if no measurement is available.
bool shutter_measure_get_latest(ShutterMeasurement* out);

// Get the rolling history (oldest first). Returns the number of entries.
uint8_t shutter_measure_get_history(ShutterMeasurement* out, uint8_t max_count);

// Get the total number of measurements taken since boot.
uint32_t shutter_measure_get_count();

// Target speed control.
// Set the comparison target by label (without trailing 's', e.g. "1/1000").
// Returns false if the label is not in the standard speed table.
bool shutter_measure_set_target(const char* label, bool recompute = true);
// Adjust the target one step faster (shorter) or slower (longer).
void shutter_measure_adjust_target(bool faster);
// Toggle the speed lock. Returns false if no target is set.
bool shutter_measure_toggle_lock();
// Set the speed lock explicitly. Returns false if enabling and no target is set.
bool shutter_measure_set_lock(bool locked);
// Check if a speed label exists in the standard speed table.
// Accepts both "1/125" and "1/125s" formats. Does not modify state.
bool shutter_measure_is_valid_speed(const char* label);
// Get the current target label and lock state.
void shutter_measure_get_target(char* out_label, size_t len, bool* out_locked);

#endif // IS_SHUTTER_TESTER
