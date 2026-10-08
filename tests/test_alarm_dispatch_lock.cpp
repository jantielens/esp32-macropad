#include "action_registry.h"
#include "action_dispatch.h"
#include "action_list.h"
#include <cassert>
#include <cstdio>
#include <cstring>

#define DISPLAY_MANAGER_H
static bool display_locked = false;
static unsigned lock_count = 0;
static unsigned resolved_count = 0;
static bool has_binding = false;
void display_manager_lock_if_needed(bool* locked) {
    *locked = !display_locked;
    if (*locked) { display_locked = true; ++lock_count; }
}
void display_manager_unlock_if_needed(bool locked) { if (locked) display_locked = false; }

static ActionResult ui_dispatch(const ButtonAction&, const char*, uint32_t) {
    assert(display_locked);
    return ACTION_FAILED;
}
static ActionResult network_dispatch(const ButtonAction&, const char*, uint32_t) {
    assert(!display_locked);
    assert(action_dispatch_context().synchronous);
    return ACTION_COMPLETE;
}
static const ActionTypeDef ui_type{"screen", nullptr, nullptr, ui_dispatch, nullptr, nullptr,
    nullptr, nullptr, nullptr, ACTION_EXECUTION_SYNC};
static const ActionTypeDef network_type{"ha_service", nullptr, nullptr, network_dispatch, nullptr, nullptr,
    nullptr, nullptr, nullptr, ACTION_EXECUTION_SYNC, false};
const ActionTypeDef* action_type_find(const char* type) {
    return !strcmp(type, "screen") ? &ui_type : !strcmp(type, "ha_service") ? &network_type : nullptr;
}
bool action_type_has_binding(const ActionTypeDef*, const ButtonAction&) { return has_binding; }
bool action_type_resolve_bindings(const ActionTypeDef*, ButtonAction&) {
    assert(display_locked);
    ++resolved_count;
    return true;
}
void action_type_collect_topics(const ActionTypeDef*, const ButtonAction&, void*) {}
void ha_service_execute() {}
void action_list_dispatch_continuation(ActionContinuationOwner) {}

#include "../src/app/action_dispatch.cpp"

int main() {
    ButtonAction action = {};
    strcpy(action.type, "screen");
    assert(action_dispatch_synchronous(action, "Alarm", nullptr, 0) == ACTION_FAILED);
    assert(!display_locked && lock_count == 1);
    display_locked = true;
    action_dispatch_synchronous(action, "Alarm", nullptr, 0);
    assert(display_locked && lock_count == 1);
    display_locked = false;
    strcpy(action.type, "ha_service");
    assert(action_dispatch_synchronous(action, "Alarm", nullptr, 0) == ACTION_COMPLETE);
    assert(lock_count == 1);
    has_binding = true;
    assert(action_dispatch_synchronous(action, "Alarm", nullptr, 0) == ACTION_COMPLETE);
    assert(resolved_count == 1 && lock_count == 2 && !display_locked);
    assert(!action_dispatch_context().synchronous);
    std::puts("alarm dispatch locks: PASS");
}