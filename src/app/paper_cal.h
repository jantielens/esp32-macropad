#pragma once

// ============================================================================
// Paper Calibration — bare-bulb Lref reading
// ============================================================================
// Compile-time gated by IS_DARKROOM_TIMER (requires HAS_DISPLAY).
//
// State machine:
//   idle ──start──→ reading (relay ON → settle → sensor read → relay OFF → done)
//   reading ──complete──→ done
//   done ──start──→ reading (re-read)
//   any ──cancel──→ idle
//
// Binding scheme "cal":
//   [cal:state]   — "idle", "reading", "done"
//   [cal:lref]    — lux reading or "---" if not read
//
// Action type "cal" (payload in mqtt_payload field):
//   "start"    — begin bare-bulb reading sequence
//   "cancel"   — abort and return to idle

// Initialize paper cal subsystem and register the "cal" binding scheme.
void paper_cal_init();

// Dispatch a cal action command string (called from action_dispatch).
void paper_cal_dispatch(const char* command);

// Tick function — call from LVGL task to drive state transitions.
void paper_cal_tick();

// Deferred I/O — call from main loop(). Handles relay + sensor sequencing.
void paper_cal_loop();
