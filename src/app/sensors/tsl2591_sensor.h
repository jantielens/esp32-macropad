#pragma once

// ============================================================================
// TSL2591 Light Sensor — demand-read driver for metering
// ============================================================================
// Compile-time gated by IS_DARKROOM_TIMER.
//
// I2C address 0x29.  Bus selection controlled by SENSOR_I2C_BUS:
//   Bus 0 — shared Wire (GT911, ES8311), uses i2c_bus mutex.
//   Bus 1 — dedicated Wire1, no mutex contention.
//
// Uses auto-gain from HIGH (428×) down to LOW (1×) with overflow fallback.
// Each read averages 3 samples at 300ms integration for noise reduction.
// Reads are blocking (~1s) — call from main loop context only.

// Initialize the TSL2591 sensor. Returns true if detected on I2C bus.
// Does NOT call Wire.begin() — assumes bus is already initialized.
bool tsl2591_init();

// Blocking lux read. Returns lux value or -1.0f on failure.
// Must be called from main loop context (not LVGL task).
float tsl2591_read_lux();

// Check if sensor responds on I2C bus.
bool tsl2591_is_connected();

// Register with sensor manager (provides name only — init/read are demand-driven).
class SensorRegistry;
void register_tsl2591_sensor(SensorRegistry &registry);
