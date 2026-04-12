#pragma once

// ============================================================================
// Expose Timer — single-exposure countdown with Shelly Plug relay control
// ============================================================================
// Compile-time gated by IS_DARKROOM_TIMER (requires HAS_DISPLAY).
//
// State machine:
//   stopped ──start──→ running (relay ON, countdown begins)
//   stopped ──focus──→ focus   (relay ON, no countdown)
//   focus ──start────→ running (lamp stays ON, countdown begins)
//   focus ──focus_off→ stopped (relay OFF)
//   running ──pause──→ paused  (relay OFF)
//   paused ──resume─→ running  (relay ON, countdown resumes)
//   running ──expire→ stopped  (relay OFF, beep)
//   running ──stop──→ stopped  (relay OFF)
//
// Binding scheme "expose" (all time tokens default to seconds with 1 decimal):
//   [expose:time]             — exposure time setting (e.g. "8.0")
//   [expose:time;mm:ss.d]     — formatted setting
//   [expose:remaining]        — countdown remaining (e.g. "6.3")
//   [expose:remaining;mm:ss.d]— with format override
//   [expose:elapsed]          — countdown elapsed (e.g. "1.7")
//   [expose:elapsed;mm:ss.d]  — with format override
//   [expose:state]            — "stopped" / "running" / "paused" / "focus"
//   [expose:relay]            — "ON" / "OFF"
//
// Action type "expose" (payload in mqtt_payload field):
//   "start"              — start countdown (relay ON)
//   "stop"               — stop and reset (relay OFF)
//   "toggle"             — stopped→start, running→pause, paused→resume
//   "pause"              — pause countdown (relay OFF)
//   "resume"             — resume countdown (relay ON)
//   "reset"              — stop timer, keep exposure setting
//   "set_time:N.N"       — set exposure time to N.N seconds
//   "add_seconds:N.N"    — adjust exposure ±N.N seconds
//   "add_stops:N.N"      — adjust exposure ±N.N f-stops (multiply by 2^N)
//   "focus"              — relay ON without timer (for framing)
//   "focus_off"          — relay OFF (exit focus mode)
//   "focus_toggle"       — toggle focus on/off (no-op while running)

// Initialize the expose timer subsystem and register the "expose" binding scheme.
// Call once during setup() after binding_template is ready.
void expose_timer_init();

// Dispatch an expose action command string (called from action_dispatch).
void expose_timer_dispatch(const char* command);

// Tick function — call from main loop or LVGL task to detect countdown expiry.
void expose_timer_tick();

// Deferred operations — call from main loop() on internal-RAM stack.
// Processes pending Shelly HTTP requests.
void expose_timer_loop();
