#include "action_continuation.h"

#if HAS_DISPLAY || HAS_BUTTON

#include <Arduino.h>
#include <string.h>

#if __has_include(<freertos/FreeRTOS.h>)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#define ACTION_CONTINUATION_LOCK() portENTER_CRITICAL(&s_mux)
#define ACTION_CONTINUATION_UNLOCK() portEXIT_CRITICAL(&s_mux)
#else
#define ACTION_CONTINUATION_LOCK()
#define ACTION_CONTINUATION_UNLOCK()
#endif

namespace {
constexpr uint8_t MAX_REMAINING_ACTIONS = MAX_BUTTON_ACTIONS - 1;

struct ActionContinuation {
    uint32_t token;
    uint32_t deadline_ms;
    uint32_t success_due_ms;
    ButtonAction remaining[MAX_REMAINING_ACTIONS];
    uint8_t remaining_count;
    char label[ACTION_CONTINUATION_LABEL_MAX_LEN];
    ActionContinuationOwner owner;
    bool active;
    bool pending;
    bool completed;
    bool success;
    bool success_scheduled;
};

#if __has_include(<freertos/FreeRTOS.h>)
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
#endif
ActionContinuation s_continuation = {};
uint32_t s_next_token = 0;

bool token_matches(uint32_t token) {
    return s_continuation.active && token != 0 && s_continuation.token == token;
}

void clear_continuation() {
    memset(&s_continuation, 0, sizeof(s_continuation));
}
} // namespace

bool action_continuation_begin(const ButtonAction* remaining, uint8_t remaining_count,
                               const char* label, uint32_t* token,
                               ActionContinuationOwner owner) {
    if (!token || remaining_count > MAX_REMAINING_ACTIONS ||
        (remaining_count && !remaining)) {
        return false;
    }

    ACTION_CONTINUATION_LOCK();
    if (s_continuation.active) {
        ACTION_CONTINUATION_UNLOCK();
        return false;
    }

    clear_continuation();
    if (++s_next_token == 0) ++s_next_token;
    s_continuation.token = s_next_token;
    s_continuation.remaining_count = remaining_count;
    if (remaining_count) {
        memcpy(s_continuation.remaining, remaining,
               remaining_count * sizeof(ButtonAction));
    }
    strlcpy(s_continuation.label, label ? label : "Action",
            sizeof(s_continuation.label));
        s_continuation.owner = owner;
    s_continuation.active = true;
    *token = s_continuation.token;
    ACTION_CONTINUATION_UNLOCK();
    return true;
}

void action_continuation_release(uint32_t token) {
    ACTION_CONTINUATION_LOCK();
    if (token_matches(token)) clear_continuation();
    ACTION_CONTINUATION_UNLOCK();
}

void action_continuation_mark_pending(uint32_t token) {
    ACTION_CONTINUATION_LOCK();
    if (token_matches(token)) {
        s_continuation.pending = true;
        s_continuation.deadline_ms = millis() + ACTION_CONTINUATION_TIMEOUT_MS;
    }
    ACTION_CONTINUATION_UNLOCK();
}

bool action_continuation_complete(uint32_t token, bool success) {
    ACTION_CONTINUATION_LOCK();
    if (!token_matches(token)) {
        ACTION_CONTINUATION_UNLOCK();
        return false;
    }
    s_continuation.completed = true;
    s_continuation.success = success;
    ACTION_CONTINUATION_UNLOCK();
    return true;
}

bool action_continuation_is_active() {
    ACTION_CONTINUATION_LOCK();
    const bool active = s_continuation.active;
    ACTION_CONTINUATION_UNLOCK();
    return active;
}

bool action_continuation_schedule_success(uint32_t token, uint32_t duration_ms) {
    ACTION_CONTINUATION_LOCK();
    if (!token_matches(token)) {
        ACTION_CONTINUATION_UNLOCK();
        return false;
    }
    s_continuation.success_due_ms = millis() + duration_ms;
    s_continuation.success_scheduled = true;
    ACTION_CONTINUATION_UNLOCK();
    return true;
}

ActionContinuationTakeResult action_continuation_take(
    ButtonAction* out_actions, uint8_t* out_count,
    char* out_label, uint8_t out_label_len, ActionContinuationOwner owner) {
    if (out_count) *out_count = 0;
    if (out_label && out_label_len) out_label[0] = '\0';

    ACTION_CONTINUATION_LOCK();
    if (!s_continuation.active || !s_continuation.pending ||
        s_continuation.owner != owner) {
        ACTION_CONTINUATION_UNLOCK();
        return ACTION_CONTINUATION_NONE;
    }

    ActionContinuationTakeResult result = ACTION_CONTINUATION_NONE;
    if (s_continuation.completed) {
        result = s_continuation.success ? ACTION_CONTINUATION_SUCCESS
                                        : ACTION_CONTINUATION_FAILED;
    } else if (s_continuation.success_scheduled &&
               (int32_t)(millis() - s_continuation.success_due_ms) >= 0) {
        result = ACTION_CONTINUATION_SUCCESS;
    } else if ((int32_t)(millis() - s_continuation.deadline_ms) >= 0) {
        result = ACTION_CONTINUATION_TIMED_OUT;
    }
    if (result == ACTION_CONTINUATION_NONE) {
        ACTION_CONTINUATION_UNLOCK();
        return result;
    }

    if (out_label && out_label_len) {
        strlcpy(out_label, s_continuation.label, out_label_len);
    }
    if (result == ACTION_CONTINUATION_SUCCESS && out_actions && out_count) {
        *out_count = s_continuation.remaining_count;
        if (*out_count) {
            memcpy(out_actions, s_continuation.remaining,
                   *out_count * sizeof(ButtonAction));
        }
    }
    clear_continuation();
    ACTION_CONTINUATION_UNLOCK();
    return result;
}

#endif // HAS_DISPLAY || HAS_BUTTON