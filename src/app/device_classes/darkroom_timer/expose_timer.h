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
//   [expose:dry_down]         — dry-down compensation (percent, e.g. "8.0")
//   [expose:effective_time]   — compensated exposure time (set_time × (1 - dry_down/100))
//   [expose:effective_time;mm:ss.d] — with format override
//
// Action type "expose" (command + value as separate fields):
//   command="start"                  — start countdown (relay ON)
//   command="stop"                   — stop and reset (relay OFF)
//   command="toggle"                 — stopped→start, running→pause, paused→resume
//   command="pause"                  — pause countdown (relay OFF)
//   command="resume"                 — resume countdown (relay ON)
//   command="reset"                  — stop timer, keep exposure setting
//   command="set_time"      value=N  — set exposure time to N seconds
//   command="adjust_seconds" value=N — adjust exposure ±N seconds
//   command="adjust_stops"  value=N  — adjust exposure ±N f-stops (multiply by 2^N)
//   command="focus"                  — relay ON without timer (for framing)
//   command="focus_off"              — relay OFF (exit focus mode)
//   command="focus_toggle"           — toggle focus on/off (no-op while running)
//   command="set_dry_down"    value=N  — set dry-down compensation to N% (0–15). Rejected while running/paused.
//   command="adjust_dry_down" value=N  — adjust dry-down ±N percentage points. Rejected while running/paused.

// Initialize the expose timer subsystem and register the "expose" binding scheme.
// Call once during setup() after binding_template is ready.
void expose_timer_init();

// Dispatch an expose action. Command and value are separate fields
// (e.g. command="set_time", value="10.5").
void expose_timer_dispatch(const char* command, const char* value);

// Tick function — call from main loop or LVGL task to detect countdown expiry.
void expose_timer_tick();

// Get the current exposure time setting (seconds). Thread-safe read of a float.
float expose_timer_get_time();

// Set the exposure time (seconds). Applies clamping and snap-to-tenth.
void expose_timer_set_time(float seconds);


