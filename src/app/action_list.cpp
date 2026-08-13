#include "action_list.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_continuation.h"
#include "action_dispatch.h"
#include "action_parse.h"
#include "log_manager.h"

#include <string.h>

uint8_t action_list_parse(JsonVariant v, ButtonAction* out, uint8_t max,
                          bool filter_literal_none) {
    if (!out || max == 0) return 0;
    memset(out, 0, sizeof(ButtonAction) * max);
    if (!v.is<JsonArray>()) return 0;
    JsonArray arr = v.as<JsonArray>();
    uint8_t count = 0;
    for (size_t i = 0; i < arr.size() && count < max; i++) {
        if (!arr[i].is<JsonObject>()) continue;
        action_parse(arr[i].as<JsonObject>(), out[count]);
        if (out[count].type[0] &&
            (!filter_literal_none || strcmp(out[count].type, "none") != 0)) {
            count++;
        } else {
            memset(&out[count], 0, sizeof(ButtonAction));
        }
    }
    return count;
}

void action_list_dispatch(const ButtonAction* actions, uint8_t count, const char* label,
                          ActionContinuationOwner owner) {
    if (!actions) return;
    for (uint8_t i = 0; i < count; i++) {
        const uint8_t remaining_count = count - i - 1;
        uint32_t continuation_token = 0;
        const bool reserved = action_continuation_begin(
            actions + i + 1, remaining_count, label, &continuation_token, owner);

        const ActionResult result = action_dispatch(actions[i], label,
                                                    continuation_token);
        if (!reserved) {
            // Occupied continuation slots must not prevent unrelated synchronous
            // action arrays from running. Pending-capable actions treat token 0
            // as unavailable and return ACTION_FAILED before starting work.
            if (result == ACTION_PENDING) {
                LOGW("Action", "%s pending action started without continuation slot", label);
                return;
            }
            if (result == ACTION_FAILED) return;
            continue;
        }

        if (result == ACTION_PENDING) {
            action_continuation_mark_pending(continuation_token);
            return;
        }
        action_continuation_release(continuation_token);
        if (result == ACTION_FAILED) return;
    }
}

void action_list_dispatch_continuation(ActionContinuationOwner owner) {
    ButtonAction actions[MAX_BUTTON_ACTIONS - 1] = {};
    uint8_t count = 0;
    char label[ACTION_CONTINUATION_LABEL_MAX_LEN];
    const ActionContinuationTakeResult result = action_continuation_take(
        actions, &count, label, sizeof(label), owner);
    if (result == ACTION_CONTINUATION_SUCCESS) {
        action_list_dispatch(actions, count, label, owner);
    } else if (result == ACTION_CONTINUATION_FAILED) {
        LOGW("Action", "%s pausable action failed; remaining actions discarded", label);
    } else if (result == ACTION_CONTINUATION_TIMED_OUT) {
        LOGW("Action", "%s pausable action timed out; remaining actions discarded", label);
    }
}

#endif // HAS_DISPLAY || HAS_BUTTON
