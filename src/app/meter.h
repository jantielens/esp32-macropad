#pragma once

// ============================================================================
// Print Prep Meter — bright/dark spot metering (stateless)
// ============================================================================
// Compile-time gated by IS_DARKROOM_TIMER (requires HAS_DISPLAY).
//
// No state machine — all commands callable at any time.
// Results recompute reactively on any input change.
//
// Binding scheme "meter":
//   [meter:lref]         — Lref (lux, from read_lref or manual)
//   [meter:zone5_time]   — Zone V time (seconds, manual entry)
//   [meter:l_bright]     — bright spot reading (lux)
//   [meter:l_dark]       — dark spot reading (lux)
//   [meter:sbr]          — Subject Brightness Range (computed)
//   [meter:grade]        — recommended grade (computed)
//   [meter:grade_label]  — human-friendly grade label
//   [meter:time]         — recommended exposure time (seconds)
//
// Action type "meter" (payload in mqtt_payload field):
//   "read_lref"          — bare-bulb sensor reading → Lref + shared memory
//   "read_bright"        — take bright spot reading (any time)
//   "read_dark"          — take dark spot reading (any time)
//   "set_lref:N.N"       — manual Lref value
//   "add_lref:N.N"       — adjust Lref ±N.N
//   "set_zone5:N.N"      — set Zone V time
//   "add_zone5:N.N"      — adjust Zone V time ±N.N

// Initialize meter subsystem and register the "meter" binding scheme.
void meter_init();

// Dispatch a meter action command string (called from action_dispatch).
void meter_dispatch(const char* command);

// Deferred I/O — call from main loop(). Handles sensor reads.
void meter_loop();
