#pragma once
#include <stdint.h>
#include <stddef.h>

// ============================================================================
// Timer Engine — 3 independent count-up/count-down timers
// ============================================================================
// Compile-time gated by HAS_DISPLAY. No hardware dependency.
// Timers are runtime-only (not persisted).
// Timer configuration (mode, countdown, expire actions) is loaded from
// /config/timers.json by timer_config and applied at boot.

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
void timer_start(uint8_t id);
void timer_stop(uint8_t id);      // stop and reset to 0 (up) or preset (down)
void timer_pause(uint8_t id);
void timer_resume(uint8_t id);
void timer_reset(uint8_t id);     // reset without changing running state
void timer_toggle(uint8_t id);    // stopped→start, running→pause, paused→resume
void timer_lap(uint8_t id);       // start timer 2 fresh (convenience for step timing)

// Countdown mode: set the countdown preset in seconds
void timer_set_countdown(uint8_t id, uint32_t seconds);
void timer_set_mode(uint8_t id, TimerMode mode);

// Adjust countdown preset by delta seconds (positive = add time, negative = subtract).
// Clamps countdown_ms to minimum 0. Only affects countdown-mode timers.
// If adjustment pulls timer back out of overtime, resets expire_fired so
// the expire actions can fire again if the timer crosses zero again.
void timer_adjust(uint8_t id, int32_t delta_seconds);

// Query — returns elapsed (up) or remaining/overtime (down) in milliseconds.
// For countdown: before expiry returns remaining ms, after expiry returns
// overtime ms (how far past zero). Use timer_is_overtime() to check sign.
uint32_t   timer_get_ms(uint8_t id);
TimerState timer_get_state(uint8_t id);
TimerMode  timer_get_mode(uint8_t id);
bool       timer_is_expired(uint8_t id);   // countdown reached 0
bool       timer_is_overtime(uint8_t id);  // countdown running past 0

// Set expire actions — dispatched once when a countdown timer reaches 0.
// actions: array of ButtonAction (copied internally), count: number of actions.
// Requires pad_config.h and action_dispatch.h to be available.
struct ButtonAction;  // forward declaration
void timer_set_expire_actions(uint8_t id, const ButtonAction* actions, uint8_t count);

// Clear all expire actions for a timer.
void timer_clear_expire_actions(uint8_t id);

// Tick function — call periodically (e.g. every render loop iteration).
// Detects countdown expiry edge and dispatches expire actions if configured.
void timer_engine_tick();

// Format timer value into buffer. Returns number of chars written.
// Formats: "mm:ss" (default), "hh:mm:ss", "ss", "mm:ss.d" (decisecond)
// For countdown timers past zero, output is prefixed with "-".
int timer_format(uint8_t id, const char* fmt, char* out, size_t out_len);
