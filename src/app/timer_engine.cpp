#include "timer_engine.h"
#include "board_config.h"

#if HAS_DISPLAY

#include "action_list.h"
#include "pad_config.h"

#include <Arduino.h>
#include <freertos/semphr.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Timer state
// ============================================================================

struct TimerInstance {
    TimerState state;
    TimerMode  mode;
    uint32_t   start_ms;        // millis() when last started/resumed
    uint32_t   accumulated_ms;  // total elapsed before current run
    uint32_t   countdown_ms;    // preset for countdown mode
    // Expire actions (dispatched once on countdown reaching 0)
    ButtonAction expire_actions[TIMER_MAX_EXPIRE_ACTIONS];
    uint8_t    expire_action_count;
    bool       expire_fired;    // edge detector: true once actions have fired
};

static TimerInstance s_timers[TIMER_COUNT];
static SemaphoreHandle_t s_timer_mutex = nullptr;
static bool s_timer_ready = false;

// ============================================================================
// Helpers
// ============================================================================

static inline bool valid_id(uint8_t id) {
    return id >= 1 && id <= TIMER_COUNT;
}

static inline TimerInstance& get(uint8_t id) {
    return s_timers[id - 1];
}

static inline void timer_lock() {
    xSemaphoreTake(s_timer_mutex, portMAX_DELAY);
}

static inline void timer_unlock() {
    xSemaphoreGive(s_timer_mutex);
}

// Raw elapsed ms for a timer (regardless of mode)
static uint32_t raw_elapsed(const TimerInstance& t) {
    uint32_t total = t.accumulated_ms;
    if (t.state == TIMER_RUNNING) {
        total += millis() - t.start_ms;
    }
    return total;
}

static void configure_and_start_locked(TimerInstance& t, TimerMode mode,
                                       uint32_t countdown_ms,
                                       const ButtonAction* expire_actions,
                                       uint8_t expire_action_count) {
    t.mode = mode;
    t.countdown_ms = mode == TIMER_MODE_DOWN ? countdown_ms : 0;
    t.accumulated_ms = 0;
    t.start_ms = millis();
    t.state = TIMER_RUNNING;
    t.expire_fired = false;
    t.expire_action_count = mode == TIMER_MODE_DOWN
        ? (expire_action_count > TIMER_MAX_EXPIRE_ACTIONS
            ? TIMER_MAX_EXPIRE_ACTIONS : expire_action_count)
        : 0;
    memset(t.expire_actions, 0, sizeof(t.expire_actions));
    if (t.expire_action_count > 0 && expire_actions) {
        memcpy(t.expire_actions, expire_actions,
               t.expire_action_count * sizeof(ButtonAction));
    }
}

// ============================================================================
// Public API
// ============================================================================

void timer_engine_init() {
    if (!s_timer_mutex) s_timer_mutex = xSemaphoreCreateMutex();
    if (!s_timer_mutex) {
        s_timer_ready = false;
        return;
    }
    timer_lock();
    memset(s_timers, 0, sizeof(s_timers));
    s_timer_ready = true;
    timer_unlock();
}

bool timer_configure_and_start(uint8_t id, TimerMode mode, uint32_t countdown_ms,
                               const ButtonAction* expire_actions,
                               uint8_t expire_action_count) {
        if (!s_timer_ready || !valid_id(id)
            || (mode != TIMER_MODE_UP && mode != TIMER_MODE_DOWN)
            || (mode == TIMER_MODE_DOWN && countdown_ms == 0)
            || (mode == TIMER_MODE_UP && countdown_ms != 0)
            || expire_action_count > TIMER_MAX_EXPIRE_ACTIONS
            || (expire_action_count > 0 && !expire_actions)) {
        return false;
    }
    timer_lock();
    configure_and_start_locked(get(id), mode, countdown_ms,
                               expire_actions, expire_action_count);
    timer_unlock();
    return true;
}

bool timer_toggle_prepared(uint8_t id, TimerState expected_state, TimerMode mode,
                           uint32_t countdown_ms, const ButtonAction* expire_actions,
                           uint8_t expire_action_count) {
        if (!s_timer_ready || !valid_id(id)
            || (mode != TIMER_MODE_UP && mode != TIMER_MODE_DOWN)
            || (mode == TIMER_MODE_DOWN && countdown_ms == 0)
            || (mode == TIMER_MODE_UP && countdown_ms != 0)
            || expire_action_count > TIMER_MAX_EXPIRE_ACTIONS
            || (expire_action_count > 0 && !expire_actions)) {
        return false;
    }
    timer_lock();
    TimerInstance& t = get(id);
    if (t.state != expected_state) {
        timer_unlock();
        return false;
    }
    if (t.state == TIMER_STOPPED) {
        configure_and_start_locked(t, mode, countdown_ms,
                                   expire_actions, expire_action_count);
    } else if (t.state == TIMER_RUNNING) {
        t.accumulated_ms += millis() - t.start_ms;
        t.state = TIMER_PAUSED;
    } else {
        t.start_ms = millis();
        t.state = TIMER_RUNNING;
    }
    timer_unlock();
    return true;
}

bool timer_stop(uint8_t id) {
    if (!s_timer_ready || !valid_id(id)) return false;
    timer_lock();
    auto& t = get(id);
    t.accumulated_ms = 0;
    t.state = TIMER_STOPPED;
    t.expire_fired = false;
    timer_unlock();
    return true;
}

bool timer_pause(uint8_t id) {
    if (!s_timer_ready || !valid_id(id)) return false;
    timer_lock();
    auto& t = get(id);
    if (t.state == TIMER_RUNNING) {
        t.accumulated_ms += millis() - t.start_ms;
        t.state = TIMER_PAUSED;
    }
    timer_unlock();
    return true;
}

bool timer_resume(uint8_t id) {
    if (!s_timer_ready || !valid_id(id)) return false;
    timer_lock();
    auto& t = get(id);
    if (t.state == TIMER_PAUSED) {
        t.start_ms = millis();
        t.state = TIMER_RUNNING;
    }
    timer_unlock();
    return true;
}

bool timer_reset(uint8_t id) {
    if (!s_timer_ready || !valid_id(id)) return false;
    timer_lock();
    auto& t = get(id);
    t.accumulated_ms = 0;
    t.expire_fired = false;
    if (t.state == TIMER_RUNNING) {
        t.start_ms = millis();
    }
    timer_unlock();
    return true;
}

bool timer_set_countdown_ms(uint8_t id, uint32_t countdown_ms) {
    if (!s_timer_ready || !valid_id(id)) return false;
    timer_lock();
    TimerInstance& t = get(id);
    if (t.mode != TIMER_MODE_DOWN) {
        timer_unlock();
        return false;
    }
    t.countdown_ms = countdown_ms;
    if (t.expire_fired && raw_elapsed(t) < t.countdown_ms) {
        t.expire_fired = false;
    }
    timer_unlock();
    return true;
}

bool timer_adjust(uint8_t id, int32_t delta_seconds) {
    if (!s_timer_ready || !valid_id(id)) return false;
    timer_lock();
    auto& t = get(id);
    if (t.mode != TIMER_MODE_DOWN) {
        timer_unlock();
        return false;
    }
    int64_t new_ms = (int64_t)t.countdown_ms + (int64_t)delta_seconds * 1000;
    if (new_ms < 0) new_ms = 0;
    if (new_ms > UINT32_MAX) new_ms = UINT32_MAX;
    t.countdown_ms = (uint32_t)new_ms;
    // If we pulled back out of overtime, re-arm the expire actions
    if (t.expire_fired && raw_elapsed(t) < t.countdown_ms) {
        t.expire_fired = false;
    }
    timer_unlock();
    return true;
}

uint32_t timer_get_ms(uint8_t id) {
    if (!s_timer_ready || !valid_id(id)) return 0;
    timer_lock();
    auto& t = get(id);
    uint32_t elapsed = raw_elapsed(t);
    uint32_t result;
    if (t.mode == TIMER_MODE_DOWN) {
        if (elapsed >= t.countdown_ms) {
            result = elapsed - t.countdown_ms;  // overtime (past zero)
        } else {
            result = t.countdown_ms - elapsed;  // remaining
        }
    } else {
        result = elapsed;
    }
    timer_unlock();
    return result;
}

uint32_t timer_get_target_seconds(uint8_t id) {
    if (!s_timer_ready || !valid_id(id)) return 0;
    timer_lock();
    const TimerInstance& timer = get(id);
    uint32_t result = timer.mode == TIMER_MODE_DOWN
        ? timer.countdown_ms / 1000 : 0;
    timer_unlock();
    return result;
}

TimerState timer_get_state(uint8_t id) {
    if (!s_timer_ready || !valid_id(id)) return TIMER_STOPPED;
    timer_lock();
    TimerState state = get(id).state;
    timer_unlock();
    return state;
}

TimerMode timer_get_mode(uint8_t id) {
    if (!s_timer_ready || !valid_id(id)) return TIMER_MODE_UP;
    timer_lock();
    TimerMode mode = get(id).mode;
    timer_unlock();
    return mode;
}

bool timer_is_expired(uint8_t id) {
    if (!s_timer_ready || !valid_id(id)) return false;
    timer_lock();
    auto& t = get(id);
    bool expired = t.mode == TIMER_MODE_DOWN && raw_elapsed(t) >= t.countdown_ms;
    timer_unlock();
    return expired;
}

bool timer_is_overtime(uint8_t id) {
    if (!s_timer_ready || !valid_id(id)) return false;
    timer_lock();
    auto& t = get(id);
    bool overtime = t.mode == TIMER_MODE_DOWN
        && t.state != TIMER_STOPPED
        && raw_elapsed(t) > t.countdown_ms;
    timer_unlock();
    return overtime;
}

int timer_format(uint8_t id, const char* fmt, char* out, size_t out_len) {
    if (!out || out_len == 0) return 0;
    if (!s_timer_ready || !valid_id(id)) return snprintf(out, out_len, "0.0");
    timer_lock();
    const TimerInstance& t = get(id);
    uint32_t elapsed = raw_elapsed(t);
    bool overtime = t.mode == TIMER_MODE_DOWN
        && t.state != TIMER_STOPPED
        && elapsed > t.countdown_ms;
    uint32_t ms = t.mode == TIMER_MODE_DOWN
        ? (elapsed >= t.countdown_ms ? elapsed - t.countdown_ms : t.countdown_ms - elapsed)
        : elapsed;
    timer_unlock();
    uint32_t total_s = ms / 1000;
    uint32_t h = total_s / 3600;
    uint32_t m = (total_s % 3600) / 60;
    uint32_t s = total_s % 60;
    uint32_t ds = (ms % 1000) / 100;  // deciseconds
    const char* sign = overtime ? "-" : "";

    if (!fmt || !fmt[0]) {
        // Raw numeric default: seconds with decisecond precision
        return snprintf(out, out_len, "%s%u.%u", sign, (unsigned)total_s, (unsigned)ds);
    } else if (strcmp(fmt, "mm:ss") == 0) {
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
    if (!s_timer_ready) return;
    for (uint8_t i = 0; i < TIMER_COUNT; i++) {
        ButtonAction actions[TIMER_MAX_EXPIRE_ACTIONS] = {};
        uint8_t action_count = 0;
        timer_lock();
        auto& t = s_timers[i];
        if (t.mode == TIMER_MODE_DOWN
                && t.state == TIMER_RUNNING
                && !t.expire_fired
                && raw_elapsed(t) >= t.countdown_ms) {
            t.expire_fired = true;
            action_count = t.expire_action_count;
            if (action_count > 0) {
                memcpy(actions, t.expire_actions,
                       action_count * sizeof(ButtonAction));
            }
        }
        timer_unlock();
        if (action_count > 0) {
            char label[12];
            snprintf(label, sizeof(label), "T%u Expire", i + 1);
            action_list_dispatch(actions, action_count, label);
        }
    }
}

#else // !HAS_DISPLAY

void timer_engine_init() {}
void timer_engine_tick() {}

#endif // HAS_DISPLAY
