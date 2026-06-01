#pragma once

// ============================================================================
// Print Prep Meter — bright/dark spot metering + magnification compensation
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
//   [meter:mag_lux_a]    — magnification lux reading A (4 decimals, or "---")
//   [meter:mag_lux_b]    — magnification lux reading B (4 decimals, or "---")
//   [meter:mag_factor]   — lux_a / lux_b (1 decimal, or "---")
//   [meter:mag_time]     — set_time × mag_factor (1 decimal, or "---")
//
// Action type "meter" (command + value as separate fields):
//   command="read_lref"              — bare-bulb sensor reading → Lref
//   command="read_bright"            — take bright spot reading (any time)
//   command="read_dark"              — take dark spot reading (any time)
//   command="set_lref"     value=N   — manual Lref value
//   command="adjust_lref"  value=N   — adjust Lref ±N
//   command="set_zone5"    value=N   — set Zone V time
//   command="adjust_zone5" value=N   — adjust Zone V time ±N
//   command="mag_measure_a"          — sensor read → mag_lux_a
//   command="mag_measure_b"          — sensor read → mag_lux_b
//   command="mag_clear"              — reset mag_lux_a/b (cancels in-flight reads)
//
// Sensor read requests use meter_request_read() — callback-based, reject-if-busy.

// Callback type for deferred sensor reads.
// Static/non-capturing function pointers only.
// May acquire g_meter_lock internally for state mutation.
using MeterReadCallback = void (*)(float lux);

// Initialize meter subsystem and register the "meter" binding scheme.
void meter_init();

// Dispatch a meter action. Command and value are separate fields
// (e.g. command="set_lref", value="500").
void meter_dispatch(const char* command, const char* value);

// Deferred I/O — call from main loop(). Handles sensor reads.
void meter_loop();

// Request a deferred sensor read. Returns false if a read is already pending.
// Only one read can be pending at a time (reject-if-busy).
// The callback is invoked outside the portMUX critical section after the read completes.
bool meter_request_read(MeterReadCallback on_complete);

// Get the current Lref value. Thread-safe float read.
float meter_get_lref();

// Metering context getters — thread-safe float reads for print log snapshots.
float meter_get_zone5_time();
float meter_get_bright();
float meter_get_dark();
float meter_get_sbr();
float meter_get_grade();
const char* meter_get_grade_label();
float meter_get_mag_factor();
