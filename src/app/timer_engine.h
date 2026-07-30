#pragma once
#include <stdint.h>
#include <stddef.h>

// ============================================================================
// Timer Engine — 3 independent count-up/count-down timers
// ============================================================================
// Compile-time gated by HAS_DISPLAY. No hardware dependency.
// Timers are runtime-only (not persisted).
// Start/Toggle actions supply runtime mode and duration. Timer Settings stores
// per-slot expiry actions that are snapshotted on the next countdown start.

#define TIMER_COUNT 3    // timers 1..3 (index 0..2)
#define TIMER_MAX_EXPIRE_ACTIONS 3  // max actions on countdown expiry

enum TimerMode : uint8_t {
    TIMER_MODE_UP   = 0,  // stopwatch (count up from 0)
    TIMER_MODE_DOWN = 1,  // countdown (count down from preset)
};

enum TimerState : uint8_t {
    TIMER_STOPPED  = 0,
    TIMER_RUNNING  = 1,
    TIMER_PAUSED   = 2,
};

// Initialize timer engine (call once at startup).
void timer_engine_init();

// Timer control — id is 1-based (1..TIMER_COUNT)
struct ButtonAction;
bool timer_configure_and_start(uint8_t id, TimerMode mode, uint32_t countdown_ms,
                               const ButtonAction* expire_actions, uint8_t expire_action_count);
bool timer_toggle_prepared(uint8_t id, TimerState expected_state, TimerMode mode,
                           uint32_t countdown_ms, const ButtonAction* expire_actions,
                           uint8_t expire_action_count);
bool timer_stop(uint8_t id);      // stop and reset to 0 (up) or preset (down)
bool timer_pause(uint8_t id);
bool timer_resume(uint8_t id);
bool timer_reset(uint8_t id);     // reset without changing running state

// Replace a countdown preset without changing state or elapsed time.
// Returns false for invalid IDs or count-up timers.
bool timer_set_countdown_ms(uint8_t id, uint32_t countdown_ms);

// Adjust countdown preset by delta seconds (positive = add time, negative = subtract).
// Clamps countdown_ms to minimum 0. Only affects countdown-mode timers.
// If adjustment pulls timer back out of overtime, resets expire_fired so
// the expire actions can fire again if the timer crosses zero again.
bool timer_adjust(uint8_t id, int32_t delta_seconds);

// Query — returns elapsed (up) or remaining/overtime (down) in milliseconds.
// For countdown: before expiry returns remaining ms, after expiry returns
// overtime ms (how far past zero). Use timer_is_overtime() to check sign.
uint32_t   timer_get_ms(uint8_t id);
uint32_t   timer_get_target_seconds(uint8_t id);
TimerState timer_get_state(uint8_t id);
TimerMode  timer_get_mode(uint8_t id);
bool       timer_is_expired(uint8_t id);   // countdown reached 0
bool       timer_is_overtime(uint8_t id);  // countdown running past 0

// Tick function — call periodically (e.g. every render loop iteration).
// Detects countdown expiry edge and dispatches expire actions if configured.
void timer_engine_tick();

// Format timer value into buffer. Returns number of chars written.
// Default (NULL/empty): raw seconds with decisecond precision ("45.3")
// Named formats: "mm:ss", "hh:mm:ss", "ss", "mm:ss.d"
// For countdown timers past zero, output is prefixed with "-".
int timer_format(uint8_t id, const char* fmt, char* out, size_t out_len);
