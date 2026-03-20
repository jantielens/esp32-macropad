#ifndef HX711_SENSOR_H
#define HX711_SENSOR_H

#include "board_config.h"

#if HAS_SENSOR_HX711

#include "sensors/sensor_manager.h"

void register_hx711_sensor(SensorRegistry &registry);

// ---- Scale public API (called from scale_binding / action_dispatch) ----

// Get the latest EMA-smoothed weight in grams.
float hx711_get_weight();

// Get the current flow rate in g/s (computed from weight derivative).
float hx711_get_flow_rate();

// Tare (zero) the scale. Blocks briefly for averaging.
void hx711_tare();

// Request deferred tare (non-blocking, runs on main loop next cycle).
void hx711_request_tare();

// Request deferred tare without persisting to NVS.  Use this for temporary
// tares (e.g. during brew operations) to avoid flash writes that can crash
// the USB-CDC ISR when its PSRAM-resident TX buffer becomes inaccessible.
void hx711_request_tare_no_persist();

// Request deferred calibration using current cal_weight (non-blocking).
void hx711_request_calibrate();

// Apply a new calibration factor and persist to NVS.
void hx711_set_calibration(float factor);

// Get current calibration factor.
float hx711_get_calibration_factor();

// Get current raw offset (set by tare).
long hx711_get_offset();

// Read averaged raw value minus offset (for calibration).
float hx711_get_value(int times);

// Returns true if the HX711 hardware was detected and is ready.
bool hx711_is_available();

// ---- On-device calibration workflow (cal_weight) ----

// Get the current calibration reference weight (grams, runtime only).
float hx711_get_cal_weight();

// Adjust cal_weight by delta grams (clamped to >= 1).
void hx711_adjust_cal_weight(float delta);

// Set cal_weight to an absolute value (clamped to >= 1).
void hx711_set_cal_weight(float value);

// Run calibration using current cal_weight. Returns new factor, or 0 on error.
float hx711_calibrate_with_cal_weight();

// Request deferred NVS persist of calibration data (safe from any task context).
void hx711_request_persist();

// Get human-readable status string ("idle", "taring", "calibrating").
const char* hx711_get_status();

#endif // HAS_SENSOR_HX711

#endif // HX711_SENSOR_H
