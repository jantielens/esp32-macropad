#include "action_dispatch.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_continuation.h"
#include "action_list.h"
#include "action_registry.h"
#include "action_run_context.h"
#include "action_storage.h"
#include "action_validate.h"
#include "log_manager.h"
#include "binding_template.h"
#include "runtime_variables.h"
#include "execution_context.h"
#if HAS_DISPLAY
#include "display_manager_lock.h"
#include "pad_binding.h"
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
    if (!type) action_operation_for_action(act, &type);
    if (!type || action_may_pause(act)) return ACTION_FAILED;
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
    action_visit_templates(act, binding_template_collect_topics, user_data);
}
#endif

static ActionResult dispatch_operation(const ButtonAction& action, const char* label,
                                       const ActionOperationDef& operation,
                                       const ActionTypeDef& owner, uint32_t continuation_token) {
    const ActionArgumentBlock* block = action.payload.operation.arguments;
    if (action.payload.operation.descriptor != &operation || !block ||
        block->count != operation.argument_count || !operation.dispatch ||
        action_operation_validate_descriptor(operation) ||
        (operation.execution != ACTION_EXECUTION_SYNC && !continuation_token)) {
        LOGW(TAG, "%s invalid operation arguments", label);
        return ACTION_FAILED;
    }
    ActionResolvedArgument resolved[ACTION_OPERATION_MAX_ARGUMENTS] = {};
    size_t text_bytes = 0;
    for (uint8_t index = 0; index < block->count; ++index) {
        const ActionArgumentDef& argument = operation.arguments[index];
        if (action_argument_is_text(argument.type))
            text_bytes += static_cast<size_t>(argument.max_length) + 1;
    }
    char* text = text_bytes ? static_cast<char*>(action_storage_allocate(text_bytes)) : nullptr;
    if (text_bytes && !text) {
        LOGW(TAG, "%s cannot allocate operation arguments", label);
        return ACTION_FAILED;
    }
    char* next_text = text;
    const ActionStoredArgument* stored = action_storage_arguments(block);
    const char* error = nullptr;
    ActionResult transactional_result = ACTION_FAILED;
#if HAS_DISPLAY
    bool did_lock = false;
    display_manager_lock_if_needed(&did_lock);
#endif
    {
#if HAS_DISPLAY
    const ActionRunContext* context = action_run_current();
    uint8_t binding_count = 0;
    const PadBinding* bindings = action_run_bindings(context, &binding_count);
    PadBindingScope binding_scope(bindings, binding_count, context != nullptr);
#endif
    runtime_variable_transaction_lock();
    for (uint8_t index = 0; index < block->count && !error; ++index) {
        const ActionArgumentDef& argument = operation.arguments[index];
        if (stored[index].dynamic) {
            char value[BINDING_TEMPLATE_MAX_LEN];
            if (!argument.dynamic) {
                error = "dynamic argument is not permitted";
            } else if (!binding_template_well_formed(action_storage_text(block, stored[index])) ||
                       !binding_template_resolve_checked(action_storage_text(block, stored[index]),
                                                  value, sizeof(value))) {
                error = "dynamic argument could not be resolved";
            } else {
                const char* normalized = nullptr;
                error = action_argument_normalize_text(argument, value, &normalized,
                                                       &resolved[index].integer);
                if (!error && normalized) {
                    const size_t length = strlen(normalized) + 1;
                    memcpy(next_text, normalized, length);
                    resolved[index].text = next_text;
                    next_text += length;
                }
            }
        } else if (action_argument_is_text(argument.type)) {
            const char* normalized = nullptr;
            error = action_argument_normalize_text(argument, action_storage_text(block, stored[index]),
                                                   &normalized, nullptr);
            if (!error) {
                const size_t length = strlen(normalized) + 1;
                memcpy(next_text, normalized, length);
                resolved[index].text = next_text;
                next_text += length;
            }
        } else {
            resolved[index].integer = stored[index].integer;
            if ((argument.type == ACTION_ARGUMENT_INTEGER &&
                 (stored[index].integer < argument.minimum || stored[index].integer > argument.maximum ||
                  (argument.nonzero_minimum && stored[index].integer != 0 &&
                   stored[index].integer < argument.nonzero_minimum))) ||
                (argument.type == ACTION_ARGUMENT_BOOLEAN &&
                 stored[index].integer != 0 && stored[index].integer != 1))
                error = "argument out of range";
            else if (argument.type != ACTION_ARGUMENT_INTEGER && argument.type != ACTION_ARGUMENT_BOOLEAN)
                error = "unsupported argument type";
        }
    }
    if (!error && operation.variable_transaction)
        transactional_result = operation.dispatch(resolved, label, continuation_token);
    runtime_variable_transaction_unlock();
    }
#if HAS_DISPLAY
    display_manager_unlock_if_needed(did_lock);
#endif
    if (error) {
        LOGW(TAG, "%s %s: %s", label, operation.id, error);
        action_storage_free(text);
        return ACTION_FAILED;
    }
    if (operation.variable_transaction) {
        action_storage_free(text);
        return transactional_result;
    }
#if HAS_DISPLAY
    bool dispatch_locked = false;
    if (owner.dispatch_lock_always ||
        (thread_execution_context.synchronous && owner.display_lock_required))
        display_manager_lock_if_needed(&dispatch_locked);
#else
    (void)owner;
#endif
    const ActionResult result = operation.dispatch(resolved, label, continuation_token);
#if HAS_DISPLAY
    display_manager_unlock_if_needed(dispatch_locked);
#endif
    action_storage_free(text);
    return result;
}

static ActionResult action_dispatch_resolved(const ButtonAction& act, const char* label,
                                             uint32_t continuation_token);

static ActionResult dispatch_action(const ButtonAction& act_in, const char* label,
                                    uint32_t continuation_token) {
    if (!act_in.type[0]) return ACTION_COMPLETE;
    const ActionTypeDef* operation_owner = nullptr;
    const ActionOperationDef* operation = action_operation_for_action(act_in, &operation_owner);
    if (operation && operation_owner)
        return dispatch_operation(act_in, label, *operation, *operation_owner, continuation_token);

    // Ordinary bindable fields resolve before dispatch. Transactional actions
    // resolve their own templates while holding their execution transaction.
    const ActionTypeDef* type = action_type_find(act_in.type);
    if (type && type->resolve_bindings_before_dispatch && action_has_any_binding(act_in)) {
        ButtonAction act = act_in;
#if HAS_DISPLAY
        bool did_lock = false;
        display_manager_lock_if_needed(&did_lock);
#endif
        bool resolved;
        {
#if HAS_DISPLAY
            const ActionRunContext* context = action_run_current();
            uint8_t binding_count = 0;
            const PadBinding* bindings = action_run_bindings(context, &binding_count);
            PadBindingScope binding_scope(bindings, binding_count, context != nullptr);
#endif
            runtime_variable_transaction_lock();
            resolved = resolve_action_bindings(act);
            runtime_variable_transaction_unlock();
        }
#if HAS_DISPLAY
        display_manager_unlock_if_needed(did_lock);
    #endif
        if (!resolved) {
            LOGW(TAG, "%s binding failed or result exceeds action field capacity; remaining actions discarded", label);
            return ACTION_FAILED;
        }
        return action_dispatch_resolved(act, label, continuation_token);
    }
    if (type && !type->resolve_bindings_before_dispatch) {
#if HAS_DISPLAY
        const ActionRunContext* context = action_run_current();
        uint8_t binding_count = 0;
        const PadBinding* bindings = action_run_bindings(context, &binding_count);
        PadBindingScope binding_scope(bindings, binding_count, context != nullptr);
#endif
        return action_dispatch_resolved(act_in, label, continuation_token);
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
