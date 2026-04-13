#pragma once

// ============================================================================
// Print Prep Meter — bright/dark spot metering (Phase 2)
// ============================================================================
// Compile-time gated by IS_DARKROOM_TIMER (requires HAS_DISPLAY).
//
// State machine:
//   idle ──focus_on──→ awaiting_bright ──read_bright──→ awaiting_dark
//   awaiting_dark ──read_dark──→ results
//   results ──focus_on──→ awaiting_bright (re-meter)
//   any ──cancel──→ idle
//
// Binding scheme "meter":
//   [meter:state]        — "idle", "awaiting_bright", "awaiting_dark", "results"
//   [meter:lref]         — Lref (lux, auto from shared mem or manual)
//   [meter:zone5_time]   — Zone V time (seconds, manual entry)
//   [meter:l_bright]     — bright spot reading (lux)
//   [meter:l_dark]       — dark spot reading (lux)
//   [meter:sbr]          — Subject Brightness Range (computed)
//   [meter:grade]        — recommended grade (computed)
//   [meter:grade_label]  — human-friendly grade label
//   [meter:time]         — recommended exposure time (seconds)
//   [meter:relay]        — "ON" / "OFF"
//
// Action type "meter" (payload in mqtt_payload field):
//   "focus_on"           — enlarger ON, enter awaiting_bright
//   "read_bright"        — take bright spot reading
//   "read_dark"          — take dark spot reading, compute results
//   "cancel"             — abort, enlarger OFF
//   "set_lref:N.N"       — override Lref value
//   "add_lref:N.N"       — adjust Lref ±N.N
//   "set_zone5:N.N"      — set Zone V time
//   "add_zone5:N.N"      — adjust Zone V time ±N.N

// Initialize meter subsystem and register the "meter" binding scheme.
void meter_init();

// Dispatch a meter action command string (called from action_dispatch).
void meter_dispatch(const char* command);

// Tick function — call from LVGL task.
void meter_tick();

// Deferred I/O — call from main loop(). Handles sensor reads.
void meter_loop();
