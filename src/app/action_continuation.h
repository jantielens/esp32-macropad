#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "pad_config.h"

#include <stdint.h>

#define ACTION_CONTINUATION_TIMEOUT_MS 60000U
#define ACTION_CONTINUATION_LABEL_MAX_LEN 24

enum ActionContinuationTakeResult : uint8_t {
    ACTION_CONTINUATION_NONE = 0,
    ACTION_CONTINUATION_SUCCESS,
    ACTION_CONTINUATION_FAILED,
    ACTION_CONTINUATION_TIMED_OUT,
};

enum ActionContinuationOwner : uint8_t {
    ACTION_CONTINUATION_OWNER_LOOP = 0,
    ACTION_CONTINUATION_OWNER_LVGL,
};

// Reserve the single continuation slot and copy an action suffix. The caller
// must release it unless the dispatched action returns ACTION_PENDING.
bool action_continuation_begin(const ButtonAction* remaining, uint8_t remaining_count,
                               const char* label, uint32_t* token,
                               ActionContinuationOwner owner = ACTION_CONTINUATION_OWNER_LOOP);
void action_continuation_release(uint32_t token);
void action_continuation_mark_pending(uint32_t token);

// Thread-safe completion hook for asynchronous workers. It records the result;
// the saved suffix resumes later on its originating dispatch task.
bool action_continuation_complete(uint32_t token, bool success);

// Reports whether the single continuation slot is currently reserved.
bool action_continuation_is_active();

// Arrange successful completion after duration_ms. Used by the built-in delay
// action; dispatch still resumes only from the normal owner task.
bool action_continuation_schedule_success(uint32_t token, uint32_t duration_ms);

// Called by a potential action-dispatch owner. Copies and clears a completed or
// expired slot only when owner matches the originating task. out_label receives
// the saved label for every terminal result; action data is supplied on success.
ActionContinuationTakeResult action_continuation_take(
    ButtonAction* out_actions, uint8_t* out_count,
    char* out_label, uint8_t out_label_len,
    ActionContinuationOwner owner = ACTION_CONTINUATION_OWNER_LOOP);

#endif // HAS_DISPLAY || HAS_BUTTON