#include "action_dispatch.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_continuation.h"
#include "action_list.h"
#include "action_registry.h"
#include "log_manager.h"
#include "binding_template.h"
#include "runtime_variables.h"
#include "execution_context.h"
#if HAS_DISPLAY
#include "display_manager_lock.h"
#endif
#if HAS_DISPLAY && HAS_PSRAM
#include "item_binding.h"
#endif
#include "ha_service.h"

#define TAG "Action"

ActionDispatchContext action_dispatch_context() {
    const auto& context = thread_execution_context;
    return {context.synchronous, context.work_guard, context.generation,
            context.alarm_audio, context.interactive};
}
ActionResult action_dispatch_synchronous(const ButtonAction& act, const char* label,
                                         bool (*work_guard)(uint32_t), uint32_t generation,
                                         bool alarm_audio) {
    const ActionTypeDef* type = action_type_find(act.type);
    if (!type || type->execution != ACTION_EXECUTION_SYNC) return ACTION_FAILED;
    auto& context = thread_execution_context;
    const ActionDispatchContext previous = action_dispatch_context();
    context.synchronous = true;
    context.work_guard = work_guard;
    context.generation = generation;
    context.alarm_audio = alarm_audio;
    const ActionResult result = action_dispatch(act, label);
    context.synchronous = previous.synchronous;
    context.work_guard = previous.work_guard;
    context.generation = previous.generation;
    context.alarm_audio = previous.alarm_audio;
    context.interactive = previous.interactive;
    return result;
}

static bool resolve_action_bindings(ButtonAction& act) {
    return action_type_resolve_bindings(action_type_find(act.type), act);
}

static bool action_has_any_binding(const ButtonAction& act) {
    return action_type_has_binding(action_type_find(act.type), act);
}

#if HAS_MQTT || HAS_HA_LIVE
// Collect MQTT topics from every bindable field of an action. Mirrors the
// field set in resolve_action_bindings() so a token used only inside a button
// action still gets subscribed by mqtt_sub_store's scan.
void action_collect_binding_topics(const ButtonAction& act, void* user_data) {
    action_type_collect_topics(action_type_find(act.type), act, user_data);
}
#endif

static ActionResult action_dispatch_resolved(const ButtonAction& act, const char* label,
                                             uint32_t continuation_token);

static ActionResult dispatch_action(const ButtonAction& act_in, const char* label,
                                    uint32_t continuation_token) {
    if (!act_in.type[0]) return ACTION_COMPLETE;

    // Ordinary bindable fields resolve before dispatch. Transactional actions
    // resolve their own templates while holding their execution transaction.
    const ActionTypeDef* type = action_type_find(act_in.type);
    if (type && type->resolve_bindings_before_dispatch && action_has_any_binding(act_in)) {
        ButtonAction act = act_in;
#if HAS_DISPLAY
        bool did_lock = false;
        display_manager_lock_if_needed(&did_lock);
#endif
        runtime_variable_transaction_lock();
        const bool resolved = resolve_action_bindings(act);
        runtime_variable_transaction_unlock();
#if HAS_DISPLAY
        display_manager_unlock_if_needed(did_lock);
    #endif
        if (!resolved) {
            LOGW(TAG, "%s binding failed or result exceeds action field capacity; remaining actions discarded", label);
            return ACTION_FAILED;
        }
        return action_dispatch_resolved(act, label, continuation_token);
    }
    return action_dispatch_resolved(act_in, label, continuation_token);
}

ActionResult action_dispatch(const ButtonAction& act, const char* label,
                             uint32_t continuation_token, bool interactive) {
    auto& context = thread_execution_context;
    const bool previous = context.interactive;
    context.interactive = interactive;
    const ActionResult result = dispatch_action(act, label, continuation_token);
    context.interactive = previous;
    return result;
}

static ActionResult action_dispatch_resolved(const ButtonAction& act, const char* label,
                                             uint32_t continuation_token) {
    const ActionTypeDef* type = action_type_find(act.type);
    if (!type || !type->dispatch) {
#if HAS_DISPLAY && HAS_PSRAM
        ItemBindingScope::end_current();
#endif
        LOGW(TAG, "%s unknown action type: '%s'", label, act.type);
        return ACTION_COMPLETE;
    }
    #if HAS_DISPLAY
        bool locked = false;
        if (type->dispatch_lock_always ||
            (thread_execution_context.synchronous && type->display_lock_required))
            display_manager_lock_if_needed(&locked);
    #endif
#if HAS_DISPLAY && HAS_PSRAM
        if (action_type_ends_item_context(type, act)) ItemBindingScope::end_current();
#endif
        const ActionResult result = type->dispatch(act, label, continuation_token);
    #if HAS_DISPLAY
        display_manager_unlock_if_needed(locked);
    #endif
        return result;
}

void action_dispatch_loop() {
    ha_service_execute();
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LOOP);
}

#endif // HAS_DISPLAY || HAS_BUTTON
