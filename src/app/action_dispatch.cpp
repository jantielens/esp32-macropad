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
#include <math.h>

#define TAG "Action"

ActionDispatchContext action_dispatch_context() {
    const auto& context = thread_execution_context;
    ActionDispatchContext result;
    result.synchronous = context.synchronous;
    result.work_guard = context.work_guard;
    result.generation = context.generation;
    result.alarm_audio = context.alarm_audio;
    result.interactive = context.interactive;
    return result;
}
ActionResult action_dispatch_synchronous(const ButtonAction& act, const char* label,
                                         bool (*work_guard)(uint32_t), uint32_t generation,
                                         bool alarm_audio) {
    const ActionOperationDef* operation = action_operation_for_action(act);
    if (!operation || operation->execution != ACTION_EXECUTION_SYNC) return ACTION_FAILED;
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
            const bool is_text = action_argument_is_text(argument.type);
            char* destination = is_text ? next_text : value;
            const size_t capacity = is_text ? static_cast<size_t>(argument.max_length) + 1 : sizeof(value);
            if (!argument.dynamic) {
                error = "dynamic argument is not permitted";
            } else if (!binding_template_well_formed(action_storage_text(block, stored[index])) ||
                       !binding_template_resolve_checked(action_storage_text(block, stored[index]),
                                                  destination, capacity)) {
                error = "dynamic argument could not be resolved";
            } else {
                const char* normalized = nullptr;
                error = action_argument_normalize_text(argument, destination, &normalized,
                                                       &resolved[index].integer, &resolved[index].number);
                if (!error && normalized) {
                    const size_t length = strlen(normalized) + 1;
                    memmove(next_text, normalized, length);
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
        } else if (argument.type == ACTION_ARGUMENT_NUMBER) {
            resolved[index].number = stored[index].number;
            if (!isfinite(stored[index].number) ||
                static_cast<double>(stored[index].number) < argument.minimum ||
                static_cast<double>(stored[index].number) > argument.maximum)
                error = "argument out of range";
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

static ActionResult dispatch_action(const ButtonAction& act_in, const char* label,
                                    uint32_t continuation_token) {
    if (!act_in.type[0]) return ACTION_COMPLETE;
    const ActionTypeDef* operation_owner = nullptr;
    const ActionOperationDef* operation = action_operation_for_action(act_in, &operation_owner);
    if (operation && operation_owner)
        return dispatch_operation(act_in, label, *operation, *operation_owner, continuation_token);

    LOGW(TAG, "%s invalid or legacy action", label);
    return ACTION_FAILED;
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

void action_dispatch_loop() {
    ha_service_execute();
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LOOP);
}

#endif // HAS_DISPLAY || HAS_BUTTON
