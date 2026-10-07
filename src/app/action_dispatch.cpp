#include "action_dispatch.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_continuation.h"
#include "action_list.h"
#include "action_registry.h"
#include "log_manager.h"
#if HAS_MQTT
#include "binding_template.h"
#endif
#if HAS_DISPLAY
#include "display_manager.h"
#endif
#include "ha_service.h"

#define TAG "Action"

static thread_local ActionDispatchContext dispatch_context;
ActionDispatchContext action_dispatch_context() { return dispatch_context; }
ActionResult action_dispatch_synchronous(const ButtonAction& act, const char* label,
                                         bool (*work_guard)(uint32_t), uint32_t generation) {
    const ActionTypeDef* type = action_type_find(act.type);
    if (!type || type->execution != ACTION_EXECUTION_SYNC) return ACTION_FAILED;
    const ActionDispatchContext previous = dispatch_context;
    dispatch_context = {true, work_guard, generation};
    const ActionResult result = action_dispatch(act, label);
    dispatch_context = previous;
    return result;
}

#if HAS_MQTT
static bool resolve_action_bindings(ButtonAction& act) {
    return action_type_resolve_bindings(action_type_find(act.type), act);
}

static bool action_has_any_binding(const ButtonAction& act) {
    return action_type_has_binding(action_type_find(act.type), act);
}

// Collect MQTT topics from every bindable field of an action. Mirrors the
// field set in resolve_action_bindings() so a token used only inside a button
// action still gets subscribed by mqtt_sub_store's scan.
void action_collect_binding_topics(const ButtonAction& act, void* user_data) {
    action_type_collect_topics(action_type_find(act.type), act, user_data);
}
#endif // HAS_MQTT

static ActionResult action_dispatch_resolved(const ButtonAction& act, const char* label,
                                             uint32_t continuation_token);

ActionResult action_dispatch(const ButtonAction& act_in, const char* label,
                             uint32_t continuation_token) {
    if (!act_in.type[0]) return ACTION_COMPLETE;

    // Resolve binding templates in value fields before dispatch.
    // binding_template_resolve accesses MQTT subscription state shared with the
    // LVGL task and may call LVGL APIs. When invoked from another task (e.g. a
    // hardware button on the loop() task), serialize against the LVGL task with
    // the display lock; lock_if_needed is a no-op when already on the LVGL task.
#if HAS_MQTT
    if (action_has_any_binding(act_in)) {
        ButtonAction act = act_in;
#if HAS_DISPLAY
        bool did_lock = false;
        display_manager_lock_if_needed(&did_lock);
        bool resolved = resolve_action_bindings(act);
        display_manager_unlock_if_needed(did_lock);
#else
        bool resolved = resolve_action_bindings(act);
#endif
        if (!resolved) {
            LOGW(TAG, "%s binding result exceeds action field capacity", label);
            return ACTION_COMPLETE;
        }
        return action_dispatch_resolved(act, label, continuation_token);
    }
    return action_dispatch_resolved(act_in, label, continuation_token);
#else
    return action_dispatch_resolved(act_in, label, continuation_token);
#endif
}

static ActionResult action_dispatch_resolved(const ButtonAction& act, const char* label,
                                             uint32_t continuation_token) {
    const ActionTypeDef* type = action_type_find(act.type);
        if (type && type->dispatch) {
    #if HAS_DISPLAY
        bool locked = false;
        if (dispatch_context.synchronous && type->display_lock_required)
            display_manager_lock_if_needed(&locked);
    #endif
        const ActionResult result = type->dispatch(act, label, continuation_token);
    #if HAS_DISPLAY
        display_manager_unlock_if_needed(locked);
    #endif
        return result;
        }
    LOGW(TAG, "%s unknown action type: '%s'", label, act.type);
    return ACTION_COMPLETE;
}

void action_dispatch_loop() {
    ha_service_execute();
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LOOP);
}

#endif // HAS_DISPLAY || HAS_BUTTON
