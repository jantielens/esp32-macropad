#include "action_run_context.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_storage.h"
#include "event_binding.h"
#include "log_manager.h"
#if HAS_DISPLAY
#include "pad_binding.h"
#endif
#if HAS_DISPLAY && HAS_PSRAM
#include "execution_context.h"
#include "item_binding.h"
#endif
#include <atomic>
#include <new>
#include <string.h>

struct ActionRunContext {
    std::atomic<uint32_t> references;
    ActionEventData event;
    bool has_event;
#if HAS_MCP
    ActionRunObserver observer;
    uint8_t next_action;
    uint8_t current_action;
#endif
#if HAS_DISPLAY
    const PadBinding* bindings;
    uint8_t binding_count;
#endif
#if HAS_DISPLAY && HAS_PSRAM
    ListSourceSnapshot* item;
#endif
};

static thread_local const ActionRunContext* current_run = nullptr;

ActionRunScope::ActionRunScope(const ActionRunContext* context) : previous_(current_run) {
    current_run = context;
}

ActionRunScope::~ActionRunScope() { current_run = previous_; }

const ActionRunContext* action_run_current() { return current_run; }

ActionRunContext* action_run_capture(const PadBinding* origin_bindings, uint8_t origin_binding_count,
                                     bool use_active_bindings) {
    const ActionEventData* source_event = event_binding_current();
    size_t value_length = source_event && source_event->value ? strlen(source_event->value) + 1 : 0;
    size_t step_length = source_event && source_event->step ? strlen(source_event->step) + 1 : 0;
    if (value_length > BINDING_TEMPLATE_MAX_LEN || step_length > BINDING_TEMPLATE_MAX_LEN) {
        LOGW("Action", "Event context exceeds capacity");
        return nullptr;
    }
#if HAS_DISPLAY
    const PadBinding* bindings = origin_bindings;
    uint8_t count = origin_binding_count;
    if (!bindings && use_active_bindings) pad_binding_get_bindings(&bindings, &count);
    if (count > PAD_MAX_BINDINGS || (count && !bindings)) {
        LOGW("Action", "Invalid originating pad bindings");
        return nullptr;
    }
    const size_t binding_bytes = count * sizeof(PadBinding);
#else
    (void)origin_bindings;
    (void)origin_binding_count;
    (void)use_active_bindings;
    const size_t binding_bytes = 0;
#endif
    void* memory = action_storage_allocate(sizeof(ActionRunContext) + binding_bytes +
                                           value_length + step_length);
    if (!memory) {
        LOGW("Action", "Cannot allocate execution context");
        return nullptr;
    }
    auto* context = new (memory) ActionRunContext{};
    context->references.store(1, std::memory_order_relaxed);
    char* cursor = reinterpret_cast<char*>(context + 1);
#if HAS_DISPLAY
    if (binding_bytes) {
        memcpy(cursor, bindings, binding_bytes);
        context->bindings = reinterpret_cast<const PadBinding*>(cursor);
        context->binding_count = count;
        cursor += binding_bytes;
    }
#endif
    context->has_event = source_event != nullptr;
    if (source_event) {
        context->event.value_kind = source_event->value_kind;
        if (value_length) {
            memcpy(cursor, source_event->value, value_length);
            context->event.value = cursor;
            cursor += value_length;
        }
        if (step_length) {
            memcpy(cursor, source_event->step, step_length);
            context->event.step = cursor;
        }
    }
#if HAS_DISPLAY && HAS_PSRAM
    if (thread_execution_context.item_scope) {
        JsonObjectConst item = thread_execution_context.item_scope->item();
        if (!item.isNull()) {
            context->item = ListSourceSnapshot::create(thread_execution_context.item_scope->source());
            if (!context->item || context->item->append(item) || context->item->seal()) {
                LOGW("Action", "Cannot capture List item");
                action_run_release(context);
                return nullptr;
            }
        }
    }
#endif
    return context;
}

void action_run_retain(ActionRunContext* context) {
    if (context) context->references.fetch_add(1, std::memory_order_relaxed);
}

void action_run_release(ActionRunContext* context) {
    if (!context || context->references.fetch_sub(1, std::memory_order_acq_rel) != 1) return;
#if HAS_MCP
    if (context->observer.data && context->observer.release)
        context->observer.release(context->observer.data);
#endif
#if HAS_DISPLAY && HAS_PSRAM
    if (context->item) context->item->release();
#endif
    context->~ActionRunContext();
    action_storage_free(context);
}

const ActionEventData* action_run_event(const ActionRunContext* context) {
    return context && context->has_event ? &context->event : nullptr;
}

#if HAS_MCP
void action_run_set_observer(ActionRunContext* context, const ActionRunObserver& observer) {
    if (context) context->observer = observer;
}

void action_run_next_action(ActionRunContext* context) {
    if (context) context->current_action = context->next_action++;
}

void action_run_action_failed(ActionRunContext* context) {
    if (context && context->observer.data && context->observer.failed)
        context->observer.failed(context->observer.data, context->current_action);
}

bool action_run_dispatch_ha(const HaServicePayload& payload, ActionResult* result) {
    const ActionRunContext* context = action_run_current();
    return context && context->observer.data && context->observer.dispatch_ha &&
        context->observer.dispatch_ha(context->observer.data, context->current_action,
                                      payload, result);
}
#endif

#if HAS_DISPLAY
const PadBinding* action_run_bindings(const ActionRunContext* context, uint8_t* count) {
    if (count) *count = context ? context->binding_count : 0;
    return context ? context->bindings : nullptr;
}
#endif

#if HAS_DISPLAY && HAS_PSRAM
ListSourceSnapshot* action_run_item(const ActionRunContext* context) {
    return context ? context->item : nullptr;
}
#endif

#endif
