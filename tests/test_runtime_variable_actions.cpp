#include "action_dispatch.h"
#include "action_list.h"
#include "action_registry.h"
#include "expr_binding.h"
#include "runtime_variables.h"
#include "var_binding.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

#if HAS_MQTT || HAS_HA_LIVE
#error Runtime variable action test requires network bindings disabled
#endif

void display_manager_lock_if_needed(bool* did_lock) {
    if (did_lock) *did_lock = false;
}

void display_manager_unlock_if_needed(bool) {}
void ha_service_execute() {}
extern "C" unsigned long millis() { return 0; }

static ButtonAction set_variable_action(const char* name, const char* value) {
    ButtonAction action = {};
    strlcpy(action.type, ACTION_TYPE_SET_VAR, sizeof(action.type));
    strlcpy(action.payload.set_var.var_name, name, sizeof(action.payload.set_var.var_name));
    strlcpy(action.payload.set_var.var_value, value, sizeof(action.payload.set_var.var_value));
    return action;
}

static bool get_equals(const char* name, const char* expected) {
    char value[RUNTIME_VARIABLE_VALUE_CAPACITY] = {};
    return runtime_variable_get(name, value, sizeof(value)) == RUNTIME_VARIABLE_FOUND &&
           std::strcmp(value, expected) == 0;
}

int main() {
    expr_binding_init();
    var_binding_init();
    if (!action_type_find(ACTION_TYPE_SET_VAR)) {
        std::puts("FAIL: Set variable action is registered");
        return 1;
    }

    const ButtonAction increment = set_variable_action(
        "counter", "[expr:[var:counter|0] + 1]");
    char value[RUNTIME_VARIABLE_VALUE_CAPACITY] = {};
    if (runtime_variable_get("counter", value, sizeof(value)) != RUNTIME_VARIABLE_MISSING ||
        action_dispatch(increment, "counter") != ACTION_COMPLETE || !get_equals("counter", "1") ||
        action_dispatch(increment, "counter") != ACTION_COMPLETE || !get_equals("counter", "2")) {
        std::puts("FAIL: counter advances from missing to one and then two");
        return 1;
    }
    if (action_dispatch(set_variable_action("counter", "0"), "counter") != ACTION_COMPLETE ||
        !get_equals("counter", "0")) {
        std::puts("FAIL: literal reset action stores zero");
        return 1;
    }
    const ButtonAction sequential[] = {increment, increment};
    if (action_list_dispatch_result(sequential, 2, "counter") != ACTION_COMPLETE ||
        !get_equals("counter", "2")) {
        std::puts("FAIL: sequential actions observe preceding writes");
        return 1;
    }
    const ButtonAction toggle = set_variable_action(
        "enabled", "[expr:[var:enabled|0] == 0 ? 1 : 0]");
    if (action_dispatch(toggle, "enabled") != ACTION_COMPLETE || !get_equals("enabled", "1") ||
        action_dispatch(toggle, "enabled") != ACTION_COMPLETE || !get_equals("enabled", "0")) {
        std::puts("FAIL: toggle expression switches between zero and one");
        return 1;
    }
    if (action_dispatch(set_variable_action("empty_value", ""), "empty_value") != ACTION_COMPLETE ||
        runtime_variable_get("empty_value", value, sizeof(value)) != RUNTIME_VARIABLE_FOUND || value[0]) {
        std::puts("FAIL: action stores a valid empty string");
        return 1;
    }
    runtime_variable_set("counter", "0");

    std::atomic<int> failures{0};
    constexpr int kWorkers = 4;
    constexpr int kIncrementsPerWorker = 100;
    std::thread workers[kWorkers];
    for (int worker = 0; worker < kWorkers; ++worker) {
        workers[worker] = std::thread([&] {
            for (int index = 0; index < kIncrementsPerWorker; ++index) {
                if (action_dispatch(increment, "counter") != ACTION_COMPLETE) ++failures;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    if (failures || !get_equals("counter", "400")) {
        std::puts("FAIL: concurrent read-modify-write actions retain every increment");
        return 1;
    }

    runtime_variable_set("stable", "before");
    const ButtonAction unresolved = set_variable_action("stable", "[var:not_set]");
    if (action_dispatch(unresolved, "stable") != ACTION_FAILED ||
        !get_equals("stable", "before")) {
        std::puts("FAIL: unresolved assignment fails without replacing the prior value");
        return 1;
    }

    char name[32];
    bool store_full = false;
    for (int index = 0; index < static_cast<int>(RUNTIME_VARIABLE_MAX_COUNT); ++index) {
        std::snprintf(name, sizeof(name), "fill_%02d", index);
        const RuntimeVariableSetResult set_result = runtime_variable_set(name, "x");
        if (set_result == RUNTIME_VARIABLE_SET_FULL) {
            store_full = true;
            break;
        }
        if (set_result != RUNTIME_VARIABLE_STORED) {
            std::puts("FAIL: test fills remaining runtime variable slots");
            return 1;
        }
    }
    if (!store_full) {
        std::puts("FAIL: test reached the fixed runtime variable capacity");
        return 1;
    }
    const ButtonAction failed_list[] = {
        set_variable_action("overflow", "full"),
        set_variable_action("after_failure", "must_not_run"),
    };
    if (action_list_dispatch_result(failed_list, 2, "capacity") != ACTION_FAILED ||
        runtime_variable_get("after_failure", name, sizeof(name)) != RUNTIME_VARIABLE_MISSING ||
        !get_equals("stable", "before")) {
        std::puts("FAIL: full-store failure preserves state and stops the remaining action list");
        return 1;
    }

    std::puts("runtime_variable_actions: PASS");
    return 0;
}