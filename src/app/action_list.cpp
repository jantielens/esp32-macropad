#include "action_list.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_continuation.h"
#include "action_dispatch.h"
#include "action_parse.h"
#include "action_registry.h"
#include "action_run_context.h"
#include "action_storage.h"
#include "event_binding.h"
#include "log_manager.h"
#if HAS_DISPLAY && HAS_PSRAM
#include "item_binding.h"
#endif

static ActionResult dispatch_with_origin(const ButtonAction* actions, uint8_t count, const char* label,
                                         ActionContinuationOwner owner, bool interactive,
                                         const PadBinding* origin_bindings, uint8_t origin_binding_count,
                                         bool use_active_bindings
#if HAS_MCP
                                         , const ActionRunObserver* observer = nullptr
#endif
                                         );

#include <string.h>

bool action_list_has_audio(const ButtonAction* actions, uint8_t count) {
    if (!actions) return false;
    for (uint8_t index = 0; index < count; ++index) {
        const ActionTypeDef* owner = nullptr;
        action_operation_for_action(actions[index], &owner);
        const char* type = owner ? owner->type_name : "";
        if (!strcmp(type, ACTION_TYPE_SOUND_ALERT) || !strcmp(type, ACTION_TYPE_ALARM_TONE) ||
            !strcmp(type, ACTION_TYPE_ALARM_MP3)) return true;
    }
    return false;
}

uint8_t action_list_parse(JsonVariant v, ButtonAction* out, uint8_t max) {
    if (!out || max == 0) return 0;
    memset(out, 0, sizeof(ButtonAction) * max);
    if (!v.is<JsonArray>()) return 0;
    JsonArray arr = v.as<JsonArray>();
    uint8_t count = 0;
    for (size_t i = 0; i < arr.size() && count < max; i++) {
        if (!arr[i].is<JsonObject>()) continue;
        action_parse(arr[i].as<JsonObject>(), out[count]);
        if (out[count].type[0]) {
            count++;
        } else {
            memset(&out[count], 0, sizeof(ButtonAction));
        }
    }
    return count;
}

ActionResult action_list_dispatch_result(const ButtonAction* actions, uint8_t count, const char* label,
                                         ActionContinuationOwner owner, bool interactive) {
    return dispatch_with_origin(actions, count, label, owner, interactive, nullptr, 0, true);
}

#if HAS_MCP
ActionResult action_list_dispatch_observed(const ButtonAction* actions, uint8_t count, const char* label,
                                          ActionContinuationOwner owner, bool interactive,
                                          const PadBinding* origin_bindings, uint8_t origin_binding_count,
                                          const ActionRunObserver& observer) {
    return dispatch_with_origin(actions, count, label, owner, interactive,
                                origin_bindings, origin_binding_count, false, &observer);
}
#endif

ActionResult action_list_dispatch_result(const ButtonAction* actions, uint8_t count, const char* label,
                                         ActionContinuationOwner owner, bool interactive,
                                         const PadBinding* origin_bindings, uint8_t origin_binding_count) {
    return dispatch_with_origin(actions, count, label, owner, interactive, origin_bindings, origin_binding_count, false);
}

void action_list_dispatch(const ButtonAction* actions, uint8_t count, const char* label,
                          ActionContinuationOwner owner, bool interactive) {
    action_list_dispatch_result(actions, count, label, owner, interactive);
}

void action_list_dispatch(const ButtonAction* actions, uint8_t count, const char* label,
                          ActionContinuationOwner owner, bool interactive,
                          const PadBinding* origin_bindings, uint8_t origin_binding_count) {
    action_list_dispatch_result(actions, count, label, owner, interactive, origin_bindings, origin_binding_count);
}

static ActionResult dispatch_in_context(const ButtonAction& action, const char* label,
                                        uint32_t token, bool interactive, ActionRunContext* context) {
    if (!context) return action_dispatch(action, label, token, interactive);
    ActionRunScope run(context);
    EventBindingScope event(action_run_event(context));
#if HAS_DISPLAY && HAS_PSRAM
    ItemBindingScope item(action_run_item(context), 0);
#endif
    return action_dispatch(action, label, token, interactive);
}

static ActionResult dispatch_sequence(const ButtonAction* actions, uint8_t count, const char* label,
                                      ActionContinuationOwner owner, bool interactive,
                                      ActionRunContext* context) {
    ActionContinuationContext continuation_context = {};
    if (context) {
        continuation_context.value = context;
        continuation_context.retain = [](void* value) { action_run_retain(static_cast<ActionRunContext*>(value)); };
        continuation_context.release = [](void* value) { action_run_release(static_cast<ActionRunContext*>(value)); };
    }
    for (uint8_t i = 0; i < count; i++) {
#if HAS_MCP
        action_run_next_action(context);
#endif
        uint32_t continuation_token = 0;
        if (action_may_pause(actions[i])) {
            const uint8_t remaining_count = count - i - 1;
            if (!action_continuation_begin(actions + i + 1, remaining_count, label,
                                           &continuation_token, owner, interactive,
                                           continuation_context)) {
                LOGW("Action", "%s cannot reserve action continuation", label);
#if HAS_MCP
                action_run_action_failed(context);
#endif
                return ACTION_FAILED;
            }
        }
        const ActionResult result = dispatch_in_context(actions[i], label,
                                                        continuation_token, interactive, context);
        if (!continuation_token) {
            if (result == ACTION_PENDING) {
                LOGW("Action", "%s pending action started without continuation slot", label);
#if HAS_MCP
                action_run_action_failed(context);
#endif
                return ACTION_FAILED;
            }
            if (result == ACTION_FAILED) {
#if HAS_MCP
                action_run_action_failed(context);
#endif
                return ACTION_FAILED;
            }
            continue;
        }

        if (result == ACTION_PENDING) {
            action_continuation_mark_pending(continuation_token);
            return ACTION_PENDING;
        }
        action_continuation_release(continuation_token);
        if (result == ACTION_FAILED) {
#if HAS_MCP
            action_run_action_failed(context);
#endif
            return ACTION_FAILED;
        }
    }
    return ACTION_COMPLETE;
}

static ActionResult dispatch_with_origin(const ButtonAction* actions, uint8_t count, const char* label,
                                         ActionContinuationOwner owner, bool interactive,
                                         const PadBinding* origin_bindings, uint8_t origin_binding_count,
                                         bool use_active_bindings
#if HAS_MCP
                                         , const ActionRunObserver* observer
#endif
                                         ) {
    if (!actions) {
#if HAS_MCP
        if (observer && observer->data && observer->release) observer->release(observer->data);
#endif
        return ACTION_COMPLETE;
    }
    ActionStorage storage = {};
    ButtonAction* snapshot = nullptr;
    ActionRunContext* context = nullptr;
    for (uint8_t index = 0; index < count; ++index) {
        if (actions[index].type[0] != '@') continue;
        snapshot = static_cast<ButtonAction*>(action_storage_allocate(count * sizeof(ButtonAction)));
        if (snapshot && action_storage_copy_actions(storage, snapshot, actions, count))
            context = action_run_capture(origin_bindings, origin_binding_count, use_active_bindings);
        if (!snapshot || !context || storage.failed) {
            LOGW("Action", "%s operation snapshot allocation failed", label);
            action_storage_clear(storage);
            action_storage_free(snapshot);
            action_run_release(context);
#if HAS_MCP
            if (observer && observer->data && observer->release) observer->release(observer->data);
#endif
            return ACTION_FAILED;
        }
#if HAS_MCP
        if (observer) action_run_set_observer(context, *observer);
#endif
        actions = snapshot;
        break;
    }
#if HAS_MCP
    if (observer && !context) {
        if (observer->data && observer->release) observer->release(observer->data);
        return ACTION_FAILED;
    }
#endif
    const ActionResult result = dispatch_sequence(actions, count, label, owner, interactive, context);
    action_run_release(context);
    action_storage_clear(storage);
    action_storage_free(snapshot);
    return result;
}

void action_list_dispatch_continuation(ActionContinuationOwner owner) {
    ButtonAction actions[MAX_BUTTON_ACTIONS - 1] = {};
    uint8_t count = 0;
    char label[ACTION_CONTINUATION_LABEL_MAX_LEN];
    bool interactive = false;
    ActionStorage storage = {};
    ActionContinuationContext context = {};
    const ActionContinuationTakeResult result = action_continuation_take(
        actions, &count, label, sizeof(label), owner, &interactive, &storage, &context);
    if (result == ACTION_CONTINUATION_SUCCESS) {
        dispatch_sequence(actions, count, label, owner, interactive,
                          static_cast<ActionRunContext*>(context.value));
    } else if (result == ACTION_CONTINUATION_FAILED) {
        LOGW("Action", "%s pausable action failed; remaining actions discarded", label);
    } else if (result == ACTION_CONTINUATION_TIMED_OUT) {
        LOGW("Action", "%s pausable action timed out; remaining actions discarded", label);
    }
    if (context.value) context.release(context.value);
    action_storage_clear(storage);
}

#endif // HAS_DISPLAY || HAS_BUTTON
