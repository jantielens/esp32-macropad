#include <ArduinoJson.h>
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <thread>
#include <freertos/semphr.h>

#include "action_dispatch.h"
#include "action_list.h"
#include "action_parse.h"
#include "action_registry.h"
#include "action_storage.h"
#include "boot_actions.h"
#include "camera_motion_actions.h"
#include "camera.h"
#include "main_loop_bridge.h"
#include "swipe_config.h"
#include "timer_config.h"
#include <LittleFS.h>

extern "C" unsigned long millis() { return 0; }

std::map<std::string, std::string> storage_test_files;
bool storage_test_fail_open = false;
size_t storage_test_write_limit = SIZE_MAX;
FakeLittleFS LittleFS;

void storage_publish_usage(bool) {}
bool storage_mount() { return true; }
bool camera_capture_save(CameraCaptureSaveTo) { return false; }
LoopBridgeResult loop_bridge_enqueue(LoopBridgeExec, const void*, size_t) {
    return LOOP_BRIDGE_UNAVAILABLE;
}
static bool defer_camera_save = false;
static LoopBridgeExec deferred_camera_exec = nullptr;
static LoopBridgeCleanup deferred_camera_cleanup = nullptr;
alignas(void*) static unsigned char deferred_camera_context[LOOP_BRIDGE_CTX_BYTES];
LoopBridgeResult loop_bridge_dispatch(LoopBridgeExec exec, const void* context, size_t context_len,
                                     uint32_t, bool* out_ok, char* out_msg, size_t out_msg_len,
                                     LoopBridgeCleanup cleanup) {
    if (!exec || !context || context_len > sizeof(deferred_camera_context)) return LOOP_BRIDGE_INVALID;
    if (defer_camera_save) {
        memcpy(deferred_camera_context, context, context_len);
        deferred_camera_exec = exec;
        deferred_camera_cleanup = cleanup;
        return LOOP_BRIDGE_TIMEOUT;
    }
    exec(context, out_ok, out_msg, out_msg_len);
    return LOOP_BRIDGE_OK;
}
ActionDispatchContext action_dispatch_context() { return {}; }
void display_manager_request_binding_refresh() {}

static const ActionArgumentDef boot_arguments[] = {
    {"value", "Value", ACTION_ARGUMENT_STRING, 0, 0, 31, true, "[var:value]", "text"}
};
static ActionResult boot_operation(const ActionResolvedArgument*, const char*, uint32_t) {
    return ACTION_COMPLETE;
}
static const ActionOperationDef boot_operations[] = {
    {"test.boot", "Boot test", boot_arguments, 1, boot_operation}
};
static const ActionTypeDef boot_type = {
    "test_boot", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    ACTION_EXECUTION_SYNC, true, true, false, nullptr, boot_operations, 1
};
REGISTER_ACTION_TYPE(boot_type);

static bool boot_write_fail = false;
bool storage_write_atomic(const char* path, const uint8_t* data, size_t length) {
    if (boot_write_fail || storage_test_fail_open || storage_test_write_limit < length) return false;
    storage_test_files[path] = std::string(reinterpret_cast<const char*>(data), length);
    return true;
}

static std::string swipe_json() {
    JsonDocument doc;
    assert(swipe_config_to_json(doc.to<JsonObject>()));
    std::string result;
    serializeJson(doc, result);
    return result;
}

static void test_swipe_action_ownership_and_atomic_save() {
    const char* path = "/config/swipe_actions.json";
    storage_test_files.erase(path);
    swipe_config_init();
    JsonDocument defaults;
    assert(swipe_config_to_json(defaults.to<JsonObject>()));
    assert(!strcmp(defaults["swipe_right"]["op"] | "", "screen.back"));
    const char* first = R"({"swipe_left":{"op":"test.boot","args":{"value":"first"}},"swipe_right":{"op":"screen.back","args":{}}})";
    const char* second = R"({"swipe_left":{"op":"test.boot","args":{"value":"second"}}})";
    assert(swipe_config_save_raw(reinterpret_cast<const uint8_t*>(first), strlen(first)));
    const std::string before = swipe_json();
    ButtonAction captured = {};
    ActionStorage storage = {};
    assert(swipe_config_copy_action(SWIPE_LEFT, captured, storage));
    JsonDocument retained;
    assert(swipe_config_to_json(retained.to<JsonObject>()));
    assert(swipe_config_save_raw(reinterpret_cast<const uint8_t*>(second), strlen(second)));
    JsonDocument snapshot;
    action_to_json(captured, snapshot.to<JsonObject>());
    assert(!strcmp(snapshot["args"]["value"] | "", "first"));
    std::string retained_json;
    serializeJson(retained, retained_json);
    assert(retained_json == before);
    action_storage_clear(storage);

    assert(swipe_config_save_raw(reinterpret_cast<const uint8_t*>(first), strlen(first)));
    const char* invalid[] = {
        R"({"swipe_left":{"op":"test.boot","args":{"value":true}}})",
        R"({"swipe_left":{"op":"unknown","args":{}}})",
        R"({"swipe_left":{"type":"test_boot","args":{"value":"x"}}})",
        R"({"swipe_left":null})"
    };
    for (const char* json : invalid) {
        assert(!swipe_config_save_raw(reinterpret_cast<const uint8_t*>(json), strlen(json)));
        assert(storage_test_files[path] == first && swipe_json() == before);
    }
    boot_write_fail = true;
    assert(!swipe_config_save_raw(reinterpret_cast<const uint8_t*>(second), strlen(second)));
    boot_write_fail = false;
    assert(storage_test_files[path] == first && swipe_json() == before);
    storage_test_files[path] = second;
    swipe_config_reload();
    assert(swipe_json().find("second") != std::string::npos);
    const std::string after_reload = swipe_json();
    storage_test_files[path] = "{invalid";
    swipe_config_reload();
    assert(swipe_json() == after_reload);
}
static std::string dispatched_boot;
void action_list_dispatch(const ButtonAction* actions, uint8_t count, const char*,
                          ActionContinuationOwner, bool) {
    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();
    for (uint8_t index = 0; index < count; ++index)
        action_to_json(actions[index], array.createNestedObject());
    dispatched_boot.clear();
    serializeJson(doc, dispatched_boot);
}

static std::string boot_json() {
    JsonDocument doc;
    assert(boot_actions_to_json(doc.to<JsonArray>()));
    std::string result;
    serializeJson(doc, result);
    return result;
}

static void test_boot_action_ownership_and_atomic_save() {
    const char* path = "/config/boot_actions.json";
    storage_test_files.erase(path);
    boot_actions_init();
    const char* first = R"({"actions":[{"op":"test.boot","args":{"value":"first"}}]})";
    const char* second = R"({"actions":[{"op":"test.boot","args":{"value":"second"}}]})";
    assert(boot_actions_save_raw(reinterpret_cast<const uint8_t*>(first), strlen(first)));
    const std::string before = boot_json();
    assert(before.find("first") != std::string::npos);
    JsonDocument retained;
    assert(boot_actions_to_json(retained.to<JsonArray>()));
    assert(boot_actions_save_raw(reinterpret_cast<const uint8_t*>(second), strlen(second)));
    std::string serialized_retained;
    serializeJson(retained, serialized_retained);
    assert(serialized_retained == before);
    assert(boot_actions_save_raw(reinterpret_cast<const uint8_t*>(first), strlen(first)));
    const char* invalid[] = {
        R"({"actions":[{"op":"test.boot","args":{"value":true}}]})",
        R"({"actions":[{"op":"unknown","args":{}}]})",
        R"({"actions":[{"type":"test_boot","args":{"value":"x"}}]})",
        R"({"actions":[null]})",
        R"({"actions":[{},{},{},{}]})"
    };
    for (const char* json : invalid) {
        assert(!boot_actions_save_raw(reinterpret_cast<const uint8_t*>(json), strlen(json)));
        assert(storage_test_files[path] == first && boot_json() == before);
    }
    boot_write_fail = true;
    assert(!boot_actions_save_raw(reinterpret_cast<const uint8_t*>(second), strlen(second)));
    boot_write_fail = false;
    assert(storage_test_files[path] == first && boot_json() == before);
    boot_actions_dispatch();
    assert(dispatched_boot == before);
    std::thread writer([&]() {
        for (int i = 0; i < 200; ++i) {
            const char* json = i % 2 ? first : second;
            assert(boot_actions_save_raw(reinterpret_cast<const uint8_t*>(json), strlen(json)));
        }
    });
    for (int i = 0; i < 500; ++i) {
        JsonDocument doc;
        const std::string json = boot_json();
        assert(!deserializeJson(doc, json));
        const char* value = doc[0]["args"]["value"] | "";
        if (strcmp(value, "first") && strcmp(value, "second"))
            std::fprintf(stderr, "Invalid boot snapshot: %s\n", json.c_str());
        assert(!strcmp(value, "first") || !strcmp(value, "second"));
    }
    writer.join();
}

static void test_camera_motion_action_ownership_and_atomic_save() {
    const char* path = "/config/camera_motion_actions.json";
    storage_test_files.erase(path);
    camera_motion_actions_init();
    const char* first = R"({"actions":[{"op":"test.boot","args":{"value":"first"}}]})";
    const char* invalid = R"({"actions":[{"op":"test.boot","args":{"value":true}}]})";
    assert(camera_motion_actions_save_raw(reinterpret_cast<const uint8_t*>(first), strlen(first)));
    JsonDocument retained;
    assert(camera_motion_actions_to_json(retained.to<JsonArray>()));
    assert(!camera_motion_actions_save_raw(reinterpret_cast<const uint8_t*>(invalid), strlen(invalid)));
    assert(storage_test_files[path] == first);
    JsonDocument invalid_doc;
    assert(!deserializeJson(invalid_doc, invalid));
    assert(!camera_motion_actions_validate(invalid_doc["actions"].as<JsonArray>()));
    boot_write_fail = true;
    const char* second = R"({"actions":[{"op":"test.boot","args":{"value":"second"}}]})";
    assert(!camera_motion_actions_save_raw(reinterpret_cast<const uint8_t*>(second), strlen(second)));
    boot_write_fail = false;
    assert(storage_test_files[path] == first);
    camera_motion_actions_on_presence_started();
    assert(dispatched_boot.find("first") != std::string::npos);
    std::string request = second;
    defer_camera_save = true;
    assert(!camera_motion_actions_save_raw(reinterpret_cast<const uint8_t*>(request.data()), request.size()));
    defer_camera_save = false;
    request.assign(request.size(), 'x');
    bool ok = false;
    char message[64] = {};
    assert(deferred_camera_exec && deferred_camera_cleanup);
    deferred_camera_exec(deferred_camera_context, &ok, message, sizeof(message));
    deferred_camera_cleanup(deferred_camera_context);
    assert(ok && storage_test_files[path] == second);
    std::string previous;
    serializeJson(retained, previous);
    assert(previous.find("first") != std::string::npos);
}

static std::string normalized_json() {
    JsonDocument doc;
    assert(timer_config_to_json(doc.to<JsonObject>()));
    std::string result;
    serializeJson(doc, result);
    return result;
}

static TimerExpirySnapshot snapshot(uint8_t id) {
    TimerExpirySnapshot result = {};
    assert(timer_config_snapshot_expiry(id, &result));
    return result;
}

static bool save(const char* json) {
    return timer_config_save_raw((const uint8_t*)json, strlen(json));
}

static void test_missing_and_malformed_exists_semantics() {
    storage_test_files.clear();
    timer_config_init();
    assert(!timer_config_exists());
    assert(snapshot(1).count == 0);
    assert(snapshot(2).count == 0);
    assert(snapshot(3).count == 0);

    storage_test_files["/config/timers.json"] = "{malformed";
    timer_config_init();
    assert(timer_config_exists());
    TimerExpirySnapshot unavailable;
    JsonDocument document;
    assert(!timer_config_snapshot_expiry(1, &unavailable));
    assert(!timer_config_to_json(document.to<JsonObject>()));
    assert(!save(R"({"1":{"expire_actions":[]}})"));
    assert(storage_test_files["/config/timers.json"] == "{malformed");
}

static void test_legacy_load_and_normalization() {
    storage_test_files["/config/timers.json"] = R"({
        "1":{"mode":"down","countdown":30,"expire_actions":[
            {"type":"test_boot"},{"type":"test_boot"}]},
        "2":{},
        "3":{"expire_actions":[{"type":"test_boot"}]}
    })";
    timer_config_init();
    assert(timer_config_exists());
    assert(snapshot(1).count == 2);
    assert(snapshot(2).count == 0);
    assert(snapshot(3).count == 1);

    JsonDocument doc;
    assert(!deserializeJson(doc, normalized_json()));
    JsonObject root = doc.as<JsonObject>();
    assert(root.size() == 3);
    assert(root["1"]["expire_actions"].as<JsonArray>().size() == 2);
    assert(!root["1"].as<JsonObject>().containsKey("mode"));
    assert(!root["1"].as<JsonObject>().containsKey("countdown"));
}

static void test_strict_writes_and_atomic_cache() {
    const char* valid = R"({"1":{"expire_actions":[]},"2":{"expire_actions":[{"type":"test_boot"}]},"3":{"expire_actions":[]}})";
    assert(save(valid));
    assert(snapshot(1).count == 0);
    assert(snapshot(2).count == 1);

    const char* invalid[] = {
        "[]", R"({"0":{"expire_actions":[]}})",
        R"({"4":{"expire_actions":[]}})", R"({"1":[]})",
        R"({"1":{"mode":"up"}})", R"({"1":{"countdown":5}})",
        R"({"1":{"other":[]}})", R"({"1":{"expire_actions":{}}})",
        R"({"1":{"expire_actions":[null]}})",
        R"({"1":{"expire_actions":[{}]}})",
        R"({"1":{"expire_actions":[{"type":"timer","timer_id":1,"timer_command":"start","timer_mode":"down","timer_value":"1234567890123456"}]}})",
        R"({"1":{"expire_actions":[{"type":"test_boot"},{"type":"test_boot"},{"type":"test_boot"},{"type":"test_boot"}]}})",
        R"({"1":{"expire_actions":[{"type":"future","payload":"keep"}]}})"
    };
    for (const char* json : invalid) {
        std::string before = storage_test_files["/config/timers.json"];
        assert(!save(json));
        assert(storage_test_files["/config/timers.json"] == before);
        assert(snapshot(2).count == 1);
    }

    storage_test_write_limit = 5;
    assert(!save(R"({"1":{"expire_actions":[{"type":"test_boot"}]}})"));
    assert(snapshot(2).count == 1);
    storage_test_write_limit = SIZE_MAX;

    assert(save(R"({"1":{"expire_actions":[{"type":"test_boot"}]}})"));
    assert(snapshot(1).count == 1);
    assert(snapshot(2).count == 0);
    JsonDocument doc;
    assert(!deserializeJson(doc, storage_test_files["/config/timers.json"]));
    assert(doc["1"]["expire_actions"][0]["type"] == "test_boot");
    assert(doc["2"]["expire_actions"].as<JsonArray>().size() == 0);
    assert(doc["3"]["expire_actions"].as<JsonArray>().size() == 0);
}

static void test_concurrent_save_and_snapshot() {
    const char* empty = R"({"1":{"expire_actions":[]}})";
    const char* full = R"({"1":{"expire_actions":[{"type":"test_boot"},{"type":"test_boot"},{"type":"test_boot"}]}})";
    assert(save(empty));
    std::thread writer([&]() {
        for (int iteration = 0; iteration < 100; iteration++) {
            assert(save((iteration & 1) ? empty : full));
        }
    });
    for (int iteration = 0; iteration < 1000; iteration++) {
        TimerExpirySnapshot current = snapshot(1);
        assert(current.count == 0 || current.count == 3);
    }
    writer.join();
}

static void test_typed_expiry_ownership_and_rejection() {
    const char* first = R"({"1":{"expire_actions":[{"op":"test.boot","args":{"value":"first"}}]}})";
    const char* second = R"({"1":{"expire_actions":[{"op":"test.boot","args":{"value":"second"}}]}})";
    assert(save(first));
    TimerExpirySnapshot retained = snapshot(1);
    JsonDocument retained_json;
    action_to_json(retained.actions[0], retained_json.to<JsonObject>());
    assert(save(second));
    assert(retained_json["args"]["value"] == "first");
    JsonDocument after;
    action_to_json(retained.actions[0], after.to<JsonObject>());
    assert(after["args"]["value"] == "first");
    const std::string before = storage_test_files["/config/timers.json"];
    const char* invalid[] = {
        R"({"1":{"expire_actions":[{"op":"test.boot","args":{"value":true}}]}})",
        R"({"1":{"expire_actions":[{"op":"unknown","args":{}}]}})",
        R"({"1":{"expire_actions":[{"type":"test_boot","op":"test.boot","args":{"value":"x"}}]}})"
    };
    for (const char* json : invalid) {
        assert(!save(json));
        assert(storage_test_files["/config/timers.json"] == before);
        assert(normalized_json().find("second") != std::string::npos);
    }
    boot_write_fail = true;
    assert(!save(first));
    boot_write_fail = false;
    assert(storage_test_files["/config/timers.json"] == before);
    timer_config_init();
    assert(normalized_json().find("second") != std::string::npos);
}

static void test_incompatible_startup_preserves_config() {
    const char* path = "/config/timers.json";
    const char* invalid[] = {
        R"({"1":{"expire_actions":[{"op":"test.boot","args":{"value":"valid"}},{"op":"unknown","args":{}}]}})",
        R"({"1":{"expire_actions":[{"op":"test.boot","args":{"value":true}}]}})",
        R"({"1":{"expire_actions":[{"type":"test_boot","op":"test.boot","args":{"value":"x"}}]}})",
        R"({"1":{"expire_actions":[{"type":"test_boot","args":{"value":"x"}}]}})",
        R"({"1":{"expire_actions":[{"type":"future","payload":"preserve"}]}})",
        R"({"1":{"expire_actions":[null]}})",
        R"({"1":{"expire_actions":[{}]}})",
        R"({"1":{"expire_actions":"bad"}})",
        R"({"1":null})",
        R"({"1":{"expire_actions":[{"type":"test_boot"},{"type":"test_boot"},{"type":"test_boot"},{"op":"test.boot","args":{"value":"last"}}]}})"
    };
    for (const char* json : invalid) {
        storage_test_files[path] = json;
        timer_config_init();
        TimerExpirySnapshot result;
        JsonDocument document;
        assert(!timer_config_snapshot_expiry(1, &result));
        assert(!timer_config_to_json(document.to<JsonObject>()));
        assert(!save(R"({"1":{"expire_actions":[]}})"));
        assert(storage_test_files[path] == json);
    }
    storage_test_files.erase(path);
    timer_config_init();
    assert(snapshot(1).count == 0);
    assert(save(R"({"1":{"expire_actions":[{"op":"test.boot","args":{"value":"restored"}}]}})"));
    timer_config_init();
    assert(normalized_json().find("restored") != std::string::npos);
    const char* visual = R"({"1":{"expire_actions":[{"type":"visual_alert","op":"start","color":"#ff0000"}]}})";
    assert(save(visual));
    timer_config_init();
    assert(normalized_json().find("\"op\":\"start\"") != std::string::npos);
}

static void test_concurrent_saves_stay_consistent() {
    const char* first = R"({"1":{"expire_actions":[{"type":"test_boot"}]}})";
    const char* second = R"({"1":{"expire_actions":[{"type":"test_boot"},{"type":"test_boot"}]}})";
    std::thread first_writer([&]() { assert(save(first)); });
    std::thread second_writer([&]() { assert(save(second)); });
    first_writer.join();
    second_writer.join();

    JsonDocument persisted;
    assert(!deserializeJson(persisted, storage_test_files["/config/timers.json"]));
    size_t persisted_count = persisted["1"]["expire_actions"].as<JsonArray>().size();
    assert(persisted_count == snapshot(1).count);
}

static void test_mutex_allocation_failure(int fail_on_call) {
    timer_test_mutex_fail_on_call = fail_on_call;
    timer_config_init();
    TimerExpirySnapshot result = {};
    assert(!timer_config_snapshot_expiry(1, &result));
    assert(!save(R"({"1":{"expire_actions":[]}})"));
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "config-mutex-failure") == 0) {
        test_mutex_allocation_failure(1);
        std::puts("timer_config config-mutex-failure: PASS");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "save-mutex-failure") == 0) {
        test_mutex_allocation_failure(2);
        std::puts("timer_config save-mutex-failure: PASS");
        return 0;
    }
    test_missing_and_malformed_exists_semantics();
    test_legacy_load_and_normalization();
    test_strict_writes_and_atomic_cache();
    test_concurrent_save_and_snapshot();
    test_typed_expiry_ownership_and_rejection();
    test_incompatible_startup_preserves_config();
    test_concurrent_saves_stay_consistent();
    test_boot_action_ownership_and_atomic_save();
    test_swipe_action_ownership_and_atomic_save();
    test_camera_motion_action_ownership_and_atomic_save();
    std::puts("timer_config: PASS");
    return 0;
}
