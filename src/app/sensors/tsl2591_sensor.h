#pragma once

// ============================================================================
// TSL2591 Light Sensor — demand-read driver for metering
// ============================================================================
// Compile-time gated by IS_DARKROOM_TIMER.
//
// I2C address 0x29 on the shared Wire bus (GT911, ES8311).
// Uses i2c_bus_lock()/i2c_bus_unlock() for thread safety.
// Reads are blocking (~100-600ms) — call from main loop context only.

// Initialize the TSL2591 sensor. Returns true if detected on I2C bus.
// Does NOT call Wire.begin() — assumes bus is already initialized.
bool tsl2591_init();

// Blocking lux read. Returns lux value or -1.0f on failure.
// Must be called from main loop context (not LVGL task).
float tsl2591_read_lux();

// Check if sensor responds on I2C bus.
bool tsl2591_is_connected();
