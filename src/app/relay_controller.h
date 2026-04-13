#pragma once

// ============================================================================
// Relay Controller — backend-agnostic enlarger relay control
// ============================================================================
// Compile-time gated by IS_DARKROOM_TIMER.
//
// Provides a single shared relay interface used by expose_timer, test_strip,
// paper_cal, and meter modules. The actual transport (Shelly HTTP, GPIO, etc.)
// is selected at runtime from DeviceConfig.
//
// Thread model:
//   relay_request()  — called from LVGL task (sets pending flag)
//   relay_loop()     — called from main loop() (processes pending HTTP/GPIO)
//   relay_is_on()    — safe to call from any context (reads volatile state)

// Initialize the relay controller. Call once during setup().
void relay_controller_init();

// Request relay state change. Safe to call from LVGL task context.
// Actual I/O is deferred to relay_loop().
void relay_request(bool on);

// Current relay state (last requested).
bool relay_is_on();

// Process pending relay commands. Call from main loop().
void relay_loop();
