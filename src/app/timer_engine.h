#pragma once
#include <stdint.h>
#include <stddef.h>

// ============================================================================
// Timer Engine — 3 independent count-up/count-down timers
// ============================================================================
// Compile-time gated by HAS_DISPLAY. No hardware dependency.
// Timers are runtime-only (not persisted).

#define TIMER_COUNT 3    // timers 1..3 (index 0..2)

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
// the expire beep can fire again if the timer crosses zero again.
void timer_adjust(uint8_t id, int32_t delta_seconds);

// Configure a beep pattern to play when a countdown timer reaches 0.
// pattern: audio beep DSL string (copied internally), NULL/"" to clear.
// volume: 0 = device volume, 1-100 = override.
void timer_set_expire_beep(uint8_t id, const char* pattern, uint8_t volume);

// Query — returns elapsed (up) or remaining/overtime (down) in milliseconds.
// For countdown: before expiry returns remaining ms, after expiry returns
// overtime ms (how far past zero). Use timer_is_overtime() to check sign.
uint32_t   timer_get_ms(uint8_t id);
TimerState timer_get_state(uint8_t id);
TimerMode  timer_get_mode(uint8_t id);
bool       timer_is_expired(uint8_t id);   // countdown reached 0
bool       timer_is_overtime(uint8_t id);  // countdown running past 0

// Tick function — call periodically (e.g. every render loop iteration).
// Detects countdown expiry edge and fires the expire beep if configured.
void timer_engine_tick();

// Format timer value into buffer. Returns number of chars written.
// Formats: "mm:ss" (default), "hh:mm:ss", "ss", "mm:ss.d" (decisecond)
// For countdown timers past zero, output is prefixed with "-".
int timer_format(uint8_t id, const char* fmt, char* out, size_t out_len);
