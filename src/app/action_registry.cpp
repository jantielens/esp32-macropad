#include "action_registry.h"

#if HAS_DISPLAY || HAS_BUTTON

#include <string.h>

#include "action_storage.h"
#include "log_manager.h"

// Shared headroom for built-in, device-class, and board-local actions.
static constexpr int MAX_ACTION_TYPES = 48;
static const ActionTypeDef* s_types[MAX_ACTION_TYPES] = {};
static int s_count = 0;
static const char* s_first_overflow = nullptr;
static unsigned s_overflow_count = 0;
static bool s_logging_ready = false;

void action_type_register(const ActionTypeDef* type) {
    if (!type || !type->type_name || action_type_find(type->type_name)) return;
    if (s_count >= MAX_ACTION_TYPES) {
        if (s_logging_ready) {
            LOGE("Action", "Registry full; cannot register '%s'", type->type_name);
        } else {
            if (!s_first_overflow) s_first_overflow = type->type_name;
            ++s_overflow_count;
        }
        return;
    }
    s_types[s_count++] = type;
}

void action_registry_logging_ready() {
    s_logging_ready = true;
    if (s_overflow_count) {
        LOGE("Action", "Registry full; %u action(s) not registered, first: '%s'",
             s_overflow_count, s_first_overflow);
        s_first_overflow = nullptr;
        s_overflow_count = 0;
    }
}

const ActionTypeDef* action_type_find(const char* type_name) {
    if (!type_name || !type_name[0]) return nullptr;
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_types[i]->type_name, type_name) == 0) return s_types[i];
    }
    return nullptr;
}

uint8_t action_type_count() { return (uint8_t)s_count; }

const ActionTypeDef* action_type_at(uint8_t index) {
    return (index < s_count) ? s_types[index] : nullptr;
}

const ActionOperationDef* action_operation_find(const char* id, const ActionTypeDef** owner) {
    if (owner) *owner = nullptr;
    if (!id || !id[0]) return nullptr;
    for (int i = 0; i < s_count; ++i) {
        const ActionTypeDef* type = s_types[i];
        if (type->available && !type->available()) continue;
        for (uint8_t j = 0; j < type->operation_count; ++j) {
            const ActionOperationDef* operation = &type->operations[j];
            if (strcmp(operation->id, id)) continue;
            if (owner) *owner = type;
            return operation;
        }
    }
    return nullptr;
}

const ActionOperationDef* action_operation_for_action(const ButtonAction& action,
                                                      const ActionTypeDef** owner) {
    if (owner) *owner = nullptr;
    if (action.type[0] != '@') return nullptr;
    for (int i = 0; i < s_count; ++i) {
        const ActionTypeDef* type = s_types[i];
        if (type->available && !type->available()) continue;
        for (uint8_t j = 0; j < type->operation_count; ++j) {
            const ActionOperationDef* operation = &type->operations[j];
            if (operation != action.payload.operation.descriptor) continue;
            if (owner) *owner = type;
            return operation;
        }
    }
    return nullptr;
}

bool action_may_pause(const ButtonAction& action) {
    const ActionOperationDef* operation = action_operation_for_action(action);
    return operation && operation->execution != ACTION_EXECUTION_SYNC;
}

bool action_type_is_supported(const char* type_name) {
    const ActionTypeDef* type = action_type_find(type_name);
    return type && (!type->available || type->available());
}

void action_visit_templates(const ButtonAction& act, ActionTemplateVisitor visitor, void* context) {
    if (!visitor || act.type[0] != '@') return;
    const ActionOperationDef* operation = action_operation_for_action(act);
    const ActionArgumentBlock* block = act.payload.operation.arguments;
    if (!operation || !block || block->count != operation->argument_count) return;
    const ActionStoredArgument* arguments = action_storage_arguments(block);
    for (uint8_t index = 0; index < block->count; ++index)
        if (arguments[index].dynamic)
            visitor(action_storage_text(block, arguments[index]), context);
}

#endif // HAS_DISPLAY || HAS_BUTTON
