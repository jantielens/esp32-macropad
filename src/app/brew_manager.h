#pragma once

#include "board_config.h"

#if HAS_DISPLAY && HAS_SENSOR_HX711

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// Brew Manager — minimal "free pour" state machine with auto-start timer
// ============================================================================
// States: IDLE → READY → BREWING → DONE
//
// - brew_start()  : tares scale, clears stats, enters READY (armed)
// - READY→BREWING : auto-transitions when weight exceeds threshold
// - brew_stop()   : stops timer, enters DONE
// - brew_reset()  : clears everything, enters IDLE
//
// The brew manager owns its own timer (raw millis()), independent of
// timer_engine. Exposes state via getter functions for the [brew:] binding.

// Brew phases
enum BrewPhase : uint8_t {
    BREW_IDLE    = 0,
    BREW_READY   = 1,   // tared, waiting for first pour
    BREW_BREWING = 2,   // timer running, pouring in progress
    BREW_DONE    = 3,   // brew finished (timer frozen)
};

// Auto-start weight threshold in grams above tare
#define BREW_AUTO_START_THRESHOLD_G  2.0f

// ---- Control API (called from action_dispatch) ----

// Tare scale, clear stats, enter READY. Safe to call from any state.
void brew_start();

// Freeze timer, enter DONE. No-op if not READY or BREWING.
void brew_stop();

// Clear all state, enter IDLE. Safe to call from any state.
void brew_reset();

// ---- Tick (called from sensor loop or main loop) ----

// Advance state machine. Must be called periodically (each sensor poll).
void brew_tick();

// ---- Query API (called from brew_binding resolver) ----

BrewPhase   brew_get_phase();
const char* brew_get_phase_name();       // "Idle", "Ready", "Brewing", "Done"
uint32_t    brew_get_timer_ms();         // elapsed ms (0 when idle/ready)
float       brew_get_weight();           // current weight from HX711
float       brew_get_flow_rate();        // current flow rate from HX711
bool        brew_is_active();            // true if READY or BREWING

// Format timer into buffer. fmt: "mm:ss", "hh:mm:ss", "ss", "mm:ss.d"
// Returns number of chars written (excl NUL).
int brew_format_timer(const char* fmt, char* out, size_t out_len);

// ---- Init ----

void brew_manager_init();

#endif // HAS_DISPLAY && HAS_SENSOR_HX711
