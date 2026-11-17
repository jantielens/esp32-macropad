#include "action_dispatch.h"
#include "action_list.h"
#include "action_registry.h"
#include "binding_template.h"
#include "expr_binding.h"
#include "runtime_variables.h"
#include "var_binding.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

static bool outer_guard(uint32_t) { return true; }
static bool inner_guard(uint32_t) { return true; }

static ActionResult inspect_sync_context(const ButtonAction& action, const char*, uint32_t) {
    const ActionDispatchContext context = action_dispatch_context();
    if (!context.synchronous || context.interactive) return ACTION_FAILED;
    if (context.generation == 202) {
        return context.work_guard == inner_guard && !context.alarm_audio
            ? ACTION_COMPLETE : ACTION_FAILED;
    }
    if (context.generation != 101 || context.work_guard != outer_guard || !context.alarm_audio ||
        action_dispatch_synchronous(action, "nested", inner_guard, 202, false) != ACTION_COMPLETE)
        return ACTION_FAILED;
    const ActionDispatchContext restored = action_dispatch_context();
    return restored.synchronous && restored.work_guard == outer_guard &&
           restored.generation == 101 && restored.alarm_audio && !restored.interactive
        ? ACTION_COMPLETE : ACTION_FAILED;
}

static const ActionTypeDef sync_context_action = {
    "sync_context", nullptr, nullptr, inspect_sync_context, nullptr, nullptr,
    nullptr, nullptr, ACTION_EXECUTION_SYNC
};

#if HAS_DISPLAY && HAS_PSRAM
#include "item_binding.h"
#include "main_loop_bridge.h"
#include "voice_notes_file.h"
#include <cstdlib>

static bool fail_psram = false;
void* heap_caps_malloc(size_t size, uint32_t) { return fail_psram ? nullptr : std::malloc(size); }
void* heap_caps_realloc(void* ptr, size_t size, uint32_t) { return fail_psram ? nullptr : std::realloc(ptr, size); }
void heap_caps_free(void* ptr) { std::free(ptr); }
bool psramFound() { return true; }

static bool boundary_received_item = false;
static bool nested_item_hidden = false;
static bool accept_load = true;
static char requested_note[64] = {};
bool voice_notes_load_request(const char* id) {
    strlcpy(requested_note, id, sizeof(requested_note));
    return accept_load;
}
void voice_notes_loaded_clear_if_matches(const char*) {}
static LoopBridgeExec delete_exec = nullptr;
alignas(uint32_t) static unsigned char delete_job[LOOP_BRIDGE_CTX_BYTES];
static char deleted_note[17] = {};
LoopBridgeResult loop_bridge_enqueue(LoopBridgeExec exec, const void* context, size_t size) {
    if (!exec || !context || size > sizeof(delete_job)) return LOOP_BRIDGE_INVALID;
    delete_exec = exec;
    std::memcpy(delete_job, context, size);
    return LOOP_BRIDGE_OK;
}
VoiceNoteFileStatus voice_notes_remove(const char* id) {
    strlcpy(deleted_note, id, sizeof(deleted_note));
    return VoiceNoteFileStatus::Ready;
}

static bool boundary_fields(ButtonAction& action, ActionFieldVisitor visitor, void* context) {
    return action_visit_field(visitor, context, "target", action.payload.screen.screen_id);
}

static void parse_boundary(const JsonObject& json, ButtonAction& action) {
    strlcpy(action.payload.screen.screen_id, json["target"] | "", sizeof(action.payload.screen.screen_id));
}

static ActionResult inspect_nested_action(const ButtonAction&, const char*, uint32_t) {
    char value[32];
    binding_template_resolve("[item:id|none]", value, sizeof(value));
    nested_item_hidden = std::strcmp(value, "none") == 0;
    return ACTION_COMPLETE;
}

static ActionResult dispatch_boundary(const ButtonAction& action, const char*, uint32_t) {
    boundary_received_item = std::strcmp(action.payload.screen.screen_id, "item-1") == 0;
    ButtonAction nested = {};
    strlcpy(nested.type, "test_nested", sizeof(nested.type));
    return action_dispatch(nested, "nested");
}

static const ActionTypeDef nested_action = {
    "test_nested", nullptr, nullptr, inspect_nested_action, nullptr, nullptr,
    nullptr, nullptr, ACTION_EXECUTION_SYNC
};
static const ActionTypeDef boundary_action = {
    "test_boundary", parse_boundary, nullptr, dispatch_boundary, boundary_fields, nullptr,
    nullptr, nullptr, ACTION_EXECUTION_SYNC, true, true, false,
    [](const ButtonAction&) { return true; }
};
#endif

#if HAS_MQTT || HAS_HA_LIVE
#error Runtime variable action test requires network bindings disabled
#endif

void display_manager_lock_if_needed(bool* did_lock) {
    if (did_lock) *did_lock = false;
}

void display_manager_unlock_if_needed(bool) {}
static unsigned binding_refreshes = 0;
void display_manager_request_binding_refresh() { ++binding_refreshes; }
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

#if HAS_DISPLAY && HAS_PSRAM
static bool item_action_validation() {
    static const ListSourceField fields[] = {
        {"id", "Stable ID", ListSourceFieldType::String, false},
        {"title", "Title", ListSourceFieldType::String, true},
    };
    static const ListSourceDescriptor source = {"[test:items]", "Test", fields, 2};
    PadBinding alias = {};
    strlcpy(alias.name, "chosen", sizeof(alias.name));
    strlcpy(alias.value, "[item:id]", sizeof(alias.value));
    JsonDocument input;
    if (deserializeJson(input, R"([
        {"type":"set_var","var_name":"chosen","var_value":"[pad:chosen]"},
        {"type":"test_boundary","target":"[item:id]"},
        {"type":"set_var","var_name":"later","var_value":"[var:chosen]"}
    ])")) return false;
    JsonArray actions = input.as<JsonArray>();
    if (item_binding_validate_actions(actions, &source, &alias, 1)) return false;
    actions[2]["var_value"] = "[pad:chosen]";
    if (!item_binding_validate_actions(actions, &source, &alias, 1)) return false;
    actions[2]["var_value"] = "[var:chosen]";
    actions[0]["var_value"] = "[item:unknown]";
    if (item_binding_validate_actions(actions, &source, &alias, 1)) return false;
    actions[0]["var_value"] = "value";
    actions[0]["var_name"] = "[item:id]";
    if (!item_binding_validate_actions(actions, &source, &alias, 1)) return false;
    actions[0]["var_name"] = "chosen";
    actions[0]["var_value"] = "[item:id]";
    if (!item_binding_validate_actions(actions, nullptr, &alias, 1)) return false;
    fail_psram = true;
    const char* allocation_error = item_binding_validate_actions(actions, &source, &alias, 1);
    fail_psram = false;
    return allocation_error && !std::strcmp(allocation_error, "action validation allocation failed");
}
#endif

int main() {
    expr_binding_init();
    var_binding_init();
    action_type_register(&sync_context_action);
    ButtonAction sync_action = {};
    strlcpy(sync_action.type, "sync_context", sizeof(sync_action.type));
    if (action_dispatch_synchronous(sync_action, "outer", outer_guard, 101, true) != ACTION_COMPLETE) {
        std::puts("FAIL: nested synchronous action context");
        return 1;
    }
    const ActionDispatchContext restored_context = action_dispatch_context();
    if (restored_context.synchronous || restored_context.work_guard || restored_context.generation ||
        restored_context.alarm_audio || restored_context.interactive) {
        std::puts("FAIL: synchronous action context restored after dispatch");
        return 1;
    }
#if HAS_DISPLAY && HAS_PSRAM
    item_binding_init();
    action_type_register(&nested_action);
    action_type_register(&boundary_action);
    if (!item_action_validation()) {
        std::puts("FAIL: item action validation");
        return 1;
    }
    ListSourceSnapshot* snapshot = ListSourceSnapshot::create();
    if (!snapshot) {
        std::puts("FAIL: create item snapshot");
        return 1;
    }
    JsonDocument input;
    input["id"] = "item-1";
    if (snapshot->append(input.as<JsonObjectConst>()) || snapshot->seal()) {
        snapshot->release();
        std::puts("FAIL: populate item snapshot");
        return 1;
    }
    {
        ListSourceSnapshot* notes = ListSourceSnapshot::create();
        JsonDocument note;
        note["id"] = "0123456789abcdef";
        if (!notes || notes->append(note.as<JsonObjectConst>()) || notes->seal()) {
            if (notes) notes->release();
            snapshot->release();
            std::puts("FAIL: create note snapshot");
            return 1;
        }
        ButtonAction deletion = {};
        strlcpy(deletion.type, ACTION_TYPE_VOICE_NOTE_DELETE, sizeof(deletion.type));
        strlcpy(deletion.payload.voice_note_delete.id, "[item:id]", sizeof(deletion.payload.voice_note_delete.id));
        const ButtonAction actions[] = {deletion, set_variable_action("after_delete", "done")};
        runtime_variable_set("after_delete", "before");
        {
            ItemBindingScope scope(notes, 0);
            if (action_list_dispatch_result(actions, 2, "delete") != ACTION_PENDING ||
                !delete_exec || !get_equals("after_delete", "before")) {
                notes->release();
                snapshot->release();
                std::puts("FAIL: note deletion resolves item ID and waits");
                return 1;
            }
        }
        bool ok = false;
        delete_exec(delete_job, &ok, nullptr, 0);
        action_list_dispatch_continuation();
        notes->release();
        if (!ok || std::strcmp(deleted_note, "0123456789abcdef") ||
            !get_equals("after_delete", "done")) {
            snapshot->release();
            std::puts("FAIL: note deletion continues after the resolved note is deleted");
            return 1;
        }
    }
    {
        ItemBindingScope outer(snapshot, 0);
        char item_value[32];
        if (action_dispatch_synchronous(sync_action, "outer", outer_guard, 101, true) != ACTION_COMPLETE) {
            snapshot->release();
            std::puts("FAIL: synchronous dispatch with item scope");
            return 1;
        }
        {
            ButtonAction load = {};
            strlcpy(load.type, ACTION_TYPE_VOICE_NOTE_LOAD, sizeof(load.type));
            strlcpy(load.payload.voice_note_load.id, "[item:id]", sizeof(load.payload.voice_note_load.id));
            const ButtonAction actions[] = {load, set_variable_action("after_load", "navigated")};
            ItemBindingScope scope(snapshot, 0);
            if (action_list_dispatch_result(actions, 2, "load") != ACTION_COMPLETE ||
                std::strcmp(requested_note, "item-1") != 0 || !get_equals("after_load", "navigated")) {
                snapshot->release();
                std::puts("FAIL: accepted item load captures the ID and continues immediately");
                return 1;
            }
        }
        {
            runtime_variable_set("after_load", "before");
            ButtonAction load = {};
            strlcpy(load.type, ACTION_TYPE_VOICE_NOTE_LOAD, sizeof(load.type));
            strlcpy(load.payload.voice_note_load.id, "[item:id]", sizeof(load.payload.voice_note_load.id));
            const ButtonAction actions[] = {load, set_variable_action("after_load", "navigated")};
            ItemBindingScope scope(snapshot, 0);
            accept_load = false;
            const ActionResult result = action_list_dispatch_result(actions, 2, "load");
            accept_load = true;
            if (result != ACTION_FAILED || !get_equals("after_load", "before")) {
                snapshot->release();
                std::puts("FAIL: rejected item load stops the action list");
                return 1;
            }
        }
        binding_template_resolve("[item:id|none]", item_value, sizeof(item_value));
        if (std::strcmp(item_value, "item-1") != 0) {
            snapshot->release();
            std::puts("FAIL: synchronous dispatch preserves item context");
            return 1;
        }
        {
            ItemBindingScope inner(snapshot, 0);
            ButtonAction boundary = {};
            strlcpy(boundary.type, "test_boundary", sizeof(boundary.type));
            strlcpy(boundary.payload.screen.screen_id, "[item:id]", sizeof(boundary.payload.screen.screen_id));
            const ButtonAction actions[] = {
                set_variable_action("captured", "[item:id]"), boundary,
                set_variable_action("after_boundary", "[item:id|none]"),
            };
            if (action_list_dispatch_result(actions, 3, "item") != ACTION_COMPLETE ||
                !get_equals("captured", "item-1") || !get_equals("after_boundary", "none") ||
                !boundary_received_item || !nested_item_hidden) {
                snapshot->release();
                std::puts("FAIL: capture precedes boundary, boundary resolves first, nested and later actions lose item context");
                return 1;
            }
        }
        char value[32];
        binding_template_resolve("[item:id|none]", value, sizeof(value));
        if (std::strcmp(value, "none") != 0) {
            snapshot->release();
            std::puts("FAIL: nested scope does not restore an ended outer context");
            return 1;
        }
    }
    {
        ItemBindingScope scope(snapshot, 0);
        ButtonAction unknown = {};
        strlcpy(unknown.type, "unknown_action", sizeof(unknown.type));
        action_dispatch(unknown, "unknown");
        char value[32];
        binding_template_resolve("[item:id|none]", value, sizeof(value));
        if (std::strcmp(value, "none") != 0) {
            snapshot->release();
            std::puts("FAIL: unknown action ends item context");
            return 1;
        }
    }
    snapshot->release();
#endif
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

    const unsigned before_refresh = binding_refreshes;
    if (action_list_dispatch_result(sequential, 2, "User", ACTION_CONTINUATION_OWNER_LVGL, true) != ACTION_COMPLETE ||
        !get_equals("counter", "2") ||
        binding_refreshes != before_refresh + (HAS_DISPLAY ? 2 : 0) ||
        action_dispatch_context().interactive) {
        std::puts("FAIL: user mutations notify the UI without leaking interaction origin");
        return 1;
    }
    const unsigned after_refresh = binding_refreshes;
    if (action_dispatch(set_variable_action("counter", "2"), "Unchanged", 0, true) != ACTION_COMPLETE ||
        action_dispatch(set_variable_action("counter", "[var:missing]"), "Failed", 0, true) != ACTION_FAILED ||
        !get_equals("counter", "2") || binding_refreshes != after_refresh) {
        std::puts("FAIL: unchanged or failed assignments do not request immediate UI work");
        return 1;
    }
    if (action_dispatch(increment, "Background") != ACTION_COMPLETE ||
        binding_refreshes != after_refresh) {
        std::puts("FAIL: unchanged or background writes do not request immediate UI work");
        return 1;
    }
    ButtonAction partial[] = {increment, set_variable_action("counter", "[var:missing]")};
    if (action_list_dispatch_result(partial, 2, "Partial", ACTION_CONTINUATION_OWNER_LVGL, true) != ACTION_FAILED ||
        !get_equals("counter", "4") ||
        binding_refreshes != after_refresh + (HAS_DISPLAY ? 1 : 0) ||
        action_dispatch_context().interactive) {
        std::puts("FAIL: successful mutations remain refreshable after a later action fails");
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