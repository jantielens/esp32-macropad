#pragma once

// ============================================================================
// TSL2591 Light Sensor — demand-read driver for metering
// ============================================================================
// Compile-time gated by IS_DARKROOM_TIMER.
//
// I2C address 0x29.  Bus selection controlled by TSL2591_I2C_BUS:
//   Bus 0 — shared Wire (GT911, ES8311), uses i2c_bus mutex.
//   Bus 1 — dedicated Wire1, no mutex contention.
//
// Uses auto-gain from HIGH (428×) down to LOW (1×) with overflow fallback.
// Each read averages 3 samples at 300ms integration for noise reduction.
// Reads are blocking (~1s) — call from main loop context only.
//
// This is a demand-read driver only. It is NOT registered with the shared
// SensorRegistry / sensor_manager — it is consumed directly by meter.cpp and
// initialized from the darkroom_timer device-class on_setup_late hook.

// Initialize the TSL2591 sensor. Returns true if detected on I2C bus.
// On a dedicated bus (TSL2591_I2C_BUS == 1) it starts Wire1 with the
// configured pins; on the shared bus it assumes the bus is already up.
bool tsl2591_init();

// Blocking lux read. Returns lux value or -1.0f on failure.
// Must be called from main loop context (not LVGL task).
float tsl2591_read_lux();

// Check if sensor responds on I2C bus.
bool tsl2591_is_connected();
