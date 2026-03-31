#pragma once

#include "board_config.h"

#if HAS_SCALE

// ============================================================================
// Scale HAL — Unified scale API that dispatches to the active sensor
// ============================================================================
// Consumers call scale_*() functions. At compile time, these resolve to the
// active sensor backend (HX711 or NAU7802). Only one scale sensor may be
// enabled per build.

// Get the latest EMA-smoothed weight in grams.
float scale_get_weight();

// Get the current flow rate in g/s (computed from weight derivative).
float scale_get_flow_rate();

// Returns true if the scale hardware was detected and is ready.
bool scale_is_available();

// Tare (zero) the scale. Blocks briefly for averaging.
void scale_tare();

// Request deferred tare (non-blocking, runs on main loop next cycle).
void scale_request_tare();

// Request deferred tare without persisting to NVS.
void scale_request_tare_no_persist();

// Request deferred calibration using current cal_weight (non-blocking).
void scale_request_calibrate();

// Apply a new calibration factor and persist to NVS.
void scale_set_calibration(float factor);

// Get current calibration factor.
float scale_get_calibration_factor();

// Get current raw offset (set by tare).
long scale_get_offset();

// Read averaged raw value minus offset (for calibration).
float scale_get_value(int times);

// Get the current calibration reference weight (grams, runtime only).
float scale_get_cal_weight();

// Adjust cal_weight by delta grams (clamped to >= 1).
void scale_adjust_cal_weight(float delta);

// Set cal_weight to an absolute value (clamped to >= 1).
void scale_set_cal_weight(float value);

// Run calibration using current cal_weight. Returns new factor, or 0 on error.
float scale_calibrate_with_cal_weight();

// Request deferred NVS persist of calibration data.
void scale_request_persist();

// Get human-readable status string ("idle", "taring", "calibrating").
const char* scale_get_status();

#endif // HAS_SCALE
