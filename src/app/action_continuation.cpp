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
static_assert(ACTION_CONTINUATION_SLOTS > 0, "at least one continuation slot is required");
static_assert(ACTION_CONTINUATION_SLOTS < UINT8_MAX,
              "continuation slots must fit uint8_t scan indices");

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
ActionContinuation s_continuations[ACTION_CONTINUATION_SLOTS] = {};
uint32_t s_next_token = 0;

ActionContinuation* find_by_token(uint32_t token) {
    if (!token) return nullptr;
    for (uint8_t index = 0; index < ACTION_CONTINUATION_SLOTS; ++index) {
        ActionContinuation& continuation = s_continuations[index];
        if (continuation.active && continuation.token == token) {
            return &continuation;
        }
    }
    return nullptr;
}

ActionContinuation* find_free_slot() {
    for (uint8_t index = 0; index < ACTION_CONTINUATION_SLOTS; ++index) {
        if (!s_continuations[index].active) return &s_continuations[index];
    }
    return nullptr;
}

void clear_continuation(ActionContinuation* continuation) {
    memset(continuation, 0, sizeof(*continuation));
}
} // namespace

bool action_continuation_begin(const ButtonAction* remaining, uint8_t remaining_count,
                               const char* label, uint32_t* token,
                               ActionContinuationOwner owner) {
    if (!token || remaining_count > MAX_REMAINING_ACTIONS ||
        (remaining_count && !remaining)) {
        return false;
    }
    *token = 0;

    ACTION_CONTINUATION_LOCK();
    ActionContinuation* continuation = find_free_slot();
    if (!continuation) {
        ACTION_CONTINUATION_UNLOCK();
        return false;
    }

    clear_continuation(continuation);
    if (++s_next_token == 0) ++s_next_token;
    continuation->token = s_next_token;
    continuation->remaining_count = remaining_count;
    if (remaining_count) {
        memcpy(continuation->remaining, remaining,
               remaining_count * sizeof(ButtonAction));
    }
    strlcpy(continuation->label, label ? label : "Action",
            sizeof(continuation->label));
    continuation->owner = owner;
    continuation->active = true;
    *token = continuation->token;
    ACTION_CONTINUATION_UNLOCK();
    return true;
}

void action_continuation_release(uint32_t token) {
    ACTION_CONTINUATION_LOCK();
    ActionContinuation* continuation = find_by_token(token);
    if (continuation) clear_continuation(continuation);
    ACTION_CONTINUATION_UNLOCK();
}

void action_continuation_mark_pending(uint32_t token) {
    ACTION_CONTINUATION_LOCK();
    ActionContinuation* continuation = find_by_token(token);
    if (continuation) {
        continuation->pending = true;
        continuation->deadline_ms = millis() + ACTION_CONTINUATION_TIMEOUT_MS;
    }
    ACTION_CONTINUATION_UNLOCK();
}

bool action_continuation_complete(uint32_t token, bool success) {
    ACTION_CONTINUATION_LOCK();
    ActionContinuation* continuation = find_by_token(token);
    if (!continuation) {
        ACTION_CONTINUATION_UNLOCK();
        return false;
    }
    continuation->completed = true;
    continuation->success = success;
    ACTION_CONTINUATION_UNLOCK();
    return true;
}

bool action_continuation_is_full() {
    ACTION_CONTINUATION_LOCK();
    bool full = true;
    for (uint8_t index = 0; index < ACTION_CONTINUATION_SLOTS; ++index) {
        if (!s_continuations[index].active) {
            full = false;
            break;
        }
    }
    ACTION_CONTINUATION_UNLOCK();
    return full;
}

bool action_continuation_schedule_success(uint32_t token, uint32_t duration_ms) {
    ACTION_CONTINUATION_LOCK();
    ActionContinuation* continuation = find_by_token(token);
    if (!continuation) {
        ACTION_CONTINUATION_UNLOCK();
        return false;
    }
    continuation->success_due_ms = millis() + duration_ms;
    continuation->success_scheduled = true;
    ACTION_CONTINUATION_UNLOCK();
    return true;
}

ActionContinuationTakeResult action_continuation_take(
    ButtonAction* out_actions, uint8_t* out_count,
    char* out_label, uint8_t out_label_len, ActionContinuationOwner owner) {
    if (out_count) *out_count = 0;
    if (out_label && out_label_len) out_label[0] = '\0';

    ACTION_CONTINUATION_LOCK();
    for (uint8_t index = 0; index < ACTION_CONTINUATION_SLOTS; ++index) {
        ActionContinuation* continuation = &s_continuations[index];
        if (!continuation->active || !continuation->pending ||
            continuation->owner != owner) {
            continue;
        }

        ActionContinuationTakeResult result = ACTION_CONTINUATION_NONE;
        if (continuation->completed) {
            result = continuation->success ? ACTION_CONTINUATION_SUCCESS
                                           : ACTION_CONTINUATION_FAILED;
        } else if (continuation->success_scheduled &&
                   (int32_t)(millis() - continuation->success_due_ms) >= 0) {
            result = ACTION_CONTINUATION_SUCCESS;
        } else if ((int32_t)(millis() - continuation->deadline_ms) >= 0) {
            result = ACTION_CONTINUATION_TIMED_OUT;
        }
        if (result == ACTION_CONTINUATION_NONE) continue;

        if (out_label && out_label_len) {
            strlcpy(out_label, continuation->label, out_label_len);
        }
        if (result == ACTION_CONTINUATION_SUCCESS && out_actions && out_count) {
            *out_count = continuation->remaining_count;
            if (*out_count) {
                memcpy(out_actions, continuation->remaining,
                       *out_count * sizeof(ButtonAction));
            }
        }
        clear_continuation(continuation);
        ACTION_CONTINUATION_UNLOCK();
        return result;
    }
    ACTION_CONTINUATION_UNLOCK();
    return ACTION_CONTINUATION_NONE;
}

#endif // HAS_DISPLAY || HAS_BUTTON