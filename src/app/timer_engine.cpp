#include "timer_engine.h"
#include "board_config.h"

#if HAS_DISPLAY

#include <Arduino.h>
#include <string.h>
#include <stdio.h>

#if HAS_AUDIO
#include "audio.h"
#endif

// ============================================================================
// Timer state
// ============================================================================

#define TIMER_EXPIRE_BEEP_MAX_LEN 128

struct TimerInstance {
    TimerState state;
    TimerMode  mode;
    uint32_t   start_ms;        // millis() when last started/resumed
    uint32_t   accumulated_ms;  // total elapsed before current run
    uint32_t   countdown_ms;    // preset for countdown mode
    // Expire beep config
    char       expire_beep[TIMER_EXPIRE_BEEP_MAX_LEN];
    uint8_t    expire_volume;   // 0 = device volume, 1-100 = override
    bool       expire_fired;    // edge detector: true once beep has fired
};

static TimerInstance s_timers[TIMER_COUNT];

// ============================================================================
// Helpers
// ============================================================================

static inline bool valid_id(uint8_t id) {
    return id >= 1 && id <= TIMER_COUNT;
}

static inline TimerInstance& get(uint8_t id) {
    return s_timers[id - 1];
}

// Raw elapsed ms for a timer (regardless of mode)
static uint32_t raw_elapsed(const TimerInstance& t) {
    uint32_t total = t.accumulated_ms;
    if (t.state == TIMER_RUNNING) {
        total += millis() - t.start_ms;
    }
    return total;
}

// ============================================================================
// Public API
// ============================================================================

void timer_engine_init() {
    memset(s_timers, 0, sizeof(s_timers));
}

void timer_start(uint8_t id) {
    if (!valid_id(id)) return;
    auto& t = get(id);
    t.accumulated_ms = 0;
    t.start_ms = millis();
    t.state = TIMER_RUNNING;
    t.expire_fired = false;
}

void timer_stop(uint8_t id) {
    if (!valid_id(id)) return;
    auto& t = get(id);
    t.accumulated_ms = 0;
    t.state = TIMER_STOPPED;
    t.expire_fired = false;
}

void timer_pause(uint8_t id) {
    if (!valid_id(id)) return;
    auto& t = get(id);
    if (t.state != TIMER_RUNNING) return;
    t.accumulated_ms += millis() - t.start_ms;
    t.state = TIMER_PAUSED;
}

void timer_resume(uint8_t id) {
    if (!valid_id(id)) return;
    auto& t = get(id);
    if (t.state != TIMER_PAUSED) return;
    t.start_ms = millis();
    t.state = TIMER_RUNNING;
}

void timer_reset(uint8_t id) {
    if (!valid_id(id)) return;
    auto& t = get(id);
    t.accumulated_ms = 0;
    t.expire_fired = false;
    if (t.state == TIMER_RUNNING) {
        t.start_ms = millis();
    }
}

void timer_toggle(uint8_t id) {
    if (!valid_id(id)) return;
    auto& t = get(id);
    switch (t.state) {
        case TIMER_STOPPED: timer_start(id);  break;
        case TIMER_RUNNING: timer_pause(id);  break;
        case TIMER_PAUSED:  timer_resume(id); break;
    }
}

void timer_lap(uint8_t id) {
    // "Lap" resets the given timer and starts it fresh
    if (!valid_id(id)) return;
    timer_start(id);
}

void timer_set_countdown(uint8_t id, uint32_t seconds) {
    if (!valid_id(id)) return;
    get(id).countdown_ms = seconds * 1000UL;
}

void timer_set_mode(uint8_t id, TimerMode mode) {
    if (!valid_id(id)) return;
    get(id).mode = mode;
}

void timer_adjust(uint8_t id, int32_t delta_seconds) {
    if (!valid_id(id)) return;
    auto& t = get(id);
    if (t.mode != TIMER_MODE_DOWN) return;
    int64_t new_ms = (int64_t)t.countdown_ms + (int64_t)delta_seconds * 1000;
    if (new_ms < 0) new_ms = 0;
    t.countdown_ms = (uint32_t)new_ms;
    // If we pulled back out of overtime, re-arm the expire beep
    if (t.expire_fired && raw_elapsed(t) < t.countdown_ms) {
        t.expire_fired = false;
    }
}

void timer_set_expire_beep(uint8_t id, const char* pattern, uint8_t volume) {
    if (!valid_id(id)) return;
    auto& t = get(id);
    if (pattern && pattern[0]) {
        strlcpy(t.expire_beep, pattern, TIMER_EXPIRE_BEEP_MAX_LEN);
    } else {
        t.expire_beep[0] = '\0';
    }
    t.expire_volume = volume;
}

uint32_t timer_get_ms(uint8_t id) {
    if (!valid_id(id)) return 0;
    auto& t = get(id);
    uint32_t elapsed = raw_elapsed(t);
    if (t.mode == TIMER_MODE_DOWN) {
        if (elapsed >= t.countdown_ms) {
            return elapsed - t.countdown_ms;  // overtime (past zero)
        }
        return t.countdown_ms - elapsed;      // remaining
    }
    return elapsed;
}

TimerState timer_get_state(uint8_t id) {
    if (!valid_id(id)) return TIMER_STOPPED;
    return get(id).state;
}

TimerMode timer_get_mode(uint8_t id) {
    if (!valid_id(id)) return TIMER_MODE_UP;
    return get(id).mode;
}

bool timer_is_expired(uint8_t id) {
    if (!valid_id(id)) return false;
    auto& t = get(id);
    if (t.mode != TIMER_MODE_DOWN) return false;
    return raw_elapsed(t) >= t.countdown_ms;
}

bool timer_is_overtime(uint8_t id) {
    if (!valid_id(id)) return false;
    auto& t = get(id);
    if (t.mode != TIMER_MODE_DOWN) return false;
    if (t.state == TIMER_STOPPED) return false;
    return raw_elapsed(t) > t.countdown_ms;
}

int timer_format(uint8_t id, const char* fmt, char* out, size_t out_len) {
    if (!out || out_len == 0) return 0;
    bool overtime = timer_is_overtime(id);
    uint32_t ms = timer_get_ms(id);
    uint32_t total_s = ms / 1000;
    uint32_t h = total_s / 3600;
    uint32_t m = (total_s % 3600) / 60;
    uint32_t s = total_s % 60;
    uint32_t ds = (ms % 1000) / 100;  // deciseconds
    const char* sign = overtime ? "-" : "";

    if (!fmt || !fmt[0] || strcmp(fmt, "mm:ss") == 0) {
        return snprintf(out, out_len, "%s%u:%02u", sign, (unsigned)(h * 60 + m), (unsigned)s);
    } else if (strcmp(fmt, "hh:mm:ss") == 0) {
        return snprintf(out, out_len, "%s%u:%02u:%02u", sign, (unsigned)h, (unsigned)m, (unsigned)s);
    } else if (strcmp(fmt, "ss") == 0) {
        return snprintf(out, out_len, "%s%u", sign, (unsigned)total_s);
    } else if (strcmp(fmt, "mm:ss.d") == 0) {
        return snprintf(out, out_len, "%s%u:%02u.%u", sign, (unsigned)(h * 60 + m), (unsigned)s, (unsigned)ds);
    }
    // Unknown format — fall back to mm:ss
    return snprintf(out, out_len, "%s%u:%02u", sign, (unsigned)(h * 60 + m), (unsigned)s);
}

void timer_engine_tick() {
    for (uint8_t i = 0; i < TIMER_COUNT; i++) {
        auto& t = s_timers[i];
        if (t.mode != TIMER_MODE_DOWN) continue;
        if (t.state != TIMER_RUNNING) continue;
        if (t.expire_fired) continue;
        if (t.expire_beep[0] == '\0') continue;
        if (raw_elapsed(t) >= t.countdown_ms) {
            t.expire_fired = true;
#if HAS_AUDIO
            audio_beep(t.expire_beep, t.expire_volume);
#endif
        }
    }
}

#else // !HAS_DISPLAY

void timer_engine_init() {}
void timer_engine_tick() {}

#endif // HAS_DISPLAY
