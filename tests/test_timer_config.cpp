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
#include "json_key_validation.h"
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
    "test_boot", nullptr, nullptr,
    ACTION_EXECUTION_SYNC, true, false, boot_operations, 1
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
    const char* first = R"({"action_format_version":1,"swipe_left":{"op":"test.boot","args":{"value":"first"}},"swipe_right":{"op":"screen.back","args":{}}})";
    const char* second = R"({"action_format_version":1,"swipe_left":{"op":"test.boot","args":{"value":"second"}}})";
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
        R"({"swipe_left":{"op":"test.boot","args":{"value":"missing version"}}})",
        R"({"action_format_version":1,"swipe_left":{"op":"test.boot","args":{"value":true}}})",
        R"({"action_format_version":1,"swipe_left":{"op":"unknown","args":{}}})",
        R"({"action_format_version":1,"swipe_left":{"type":"test_boot"}})",
        R"({"action_format_version":1,"swipe_left":null})",
        R"({"action_format_version":1,"swipe_left":{},"swipe_left\u0000x":{"type":"test_boot"}})"
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
    storage_test_files[path] = invalid[5];
    swipe_config_reload();
    assert(swipe_json() == after_reload && storage_test_files[path] == invalid[5]);

    const char* unsupported = R"({"action_format_version":1,"swipe_left":{"op":"test.boot","args":{"value":"partial"}},"swipe_right":{"op":"unknown","args":{}}})";
    storage_test_files[path] = unsupported;
    swipe_config_reload();
    JsonDocument reset;
    assert(!deserializeJson(reset, swipe_json()));
    assert(reset["config_reset"] == true);
    assert(reset["swipe_left"].as<JsonObject>().size() == 0);
    assert(reset["swipe_right"].as<JsonObject>().size() == 0);
    assert(storage_test_files[path] == unsupported);
    assert(swipe_config_save_raw(reinterpret_cast<const uint8_t*>(second), strlen(second)));
    assert(!deserializeJson(reset, swipe_json()));
    assert(reset["config_reset"] == false);
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
    const char* legacy = R"({"actions":[{"type":"screen"}]})";
    storage_test_files[path] = legacy;
    boot_actions_init();
    JsonDocument reset;
    bool config_reset = false;
    assert(boot_actions_to_json(reset.to<JsonArray>(), &config_reset));
    assert(config_reset && reset.size() == 0);
    assert(storage_test_files[path] == legacy);
    const char* first = R"({"action_format_version":1,"actions":[{"op":"test.boot","args":{"value":"first"}}]})";
    const char* second = R"({"action_format_version":1,"actions":[{"op":"test.boot","args":{"value":"second"}}]})";
    assert(boot_actions_save_raw(reinterpret_cast<const uint8_t*>(first), strlen(first)));
    assert(boot_actions_to_json(reset.to<JsonArray>(), &config_reset));
    assert(!config_reset);
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
        R"({"actions":[{"op":"test.boot","args":{"value":"missing version"}}]})",
        R"({"action_format_version":1,"actions":[{"op":"test.boot","args":{"value":true}}]})",
        R"({"action_format_version":1,"actions":[{"op":"unknown","args":{}}]})",
        R"({"action_format_version":1,"actions":[{"type":"test_boot"}]})",
        R"({"action_format_version":1,"actions":[null]})",
        R"({"action_format_version":1,"actions":[{},{},{},{}]})",
        R"({"action_format_version":1,"actions":[],"actions\u0000x":[{"type":"test_boot"}]})"
    };
    for (const char* json : invalid) {
        assert(!boot_actions_save_raw(reinterpret_cast<const uint8_t*>(json), strlen(json)));
        assert(storage_test_files[path] == first && boot_json() == before);
    }
    std::string nul_suffix = second;
    nul_suffix.push_back('\0');
    nul_suffix += R"({"type":"legacy"})";
    assert(!boot_actions_save_raw(reinterpret_cast<const uint8_t*>(nul_suffix.data()), nul_suffix.size()));
    assert(storage_test_files[path] == first && boot_json() == before);
    storage_test_files["/config/nul-scan-test.json"] = nul_suffix;
    File corrupt = LittleFS.open("/config/nul-scan-test.json", "r");
    assert(json_stream_scan_keys(corrupt) == JsonKeyScanResult::InvalidNul);
    corrupt.close();
    storage_test_files.erase("/config/nul-scan-test.json");
    std::string nul_field = second;
    nul_field.insert(nul_field.find("actions") + 3, 1, '\0');
    assert(!boot_actions_save_raw(reinterpret_cast<const uint8_t*>(nul_field.data()), nul_field.size()));
    assert(storage_test_files[path] == first && boot_json() == before);
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
    const char* first = R"({"action_format_version":1,"actions":[{"op":"test.boot","args":{"value":"first"}}]})";
    const char* invalid = R"({"action_format_version":1,"actions":[{"op":"test.boot","args":{"value":true}}]})";
    const char* incompatible[] = {
        R"({"actions":[{"type":"test_boot"}]})",
        R"({"action_format_version":1,"actions":[{"op":"test.boot","args":{"value":"valid"}},{"op":"missing.operation","args":{}}]})",
    };
    for (const char* json : incompatible) {
        storage_test_files[path] = json;
        camera_motion_actions_init();
        JsonDocument defaults;
        bool config_reset = false;
        assert(camera_motion_actions_to_json(defaults.to<JsonArray>(), &config_reset));
        assert(config_reset && defaults.size() == 0 && storage_test_files[path] == json);
        boot_write_fail = true;
        assert(!camera_motion_actions_save_raw(reinterpret_cast<const uint8_t*>(first), strlen(first)));
        boot_write_fail = false;
        assert(camera_motion_actions_to_json(defaults.to<JsonArray>(), &config_reset));
        assert(config_reset && storage_test_files[path] == json);
        assert(camera_motion_actions_save_raw(reinterpret_cast<const uint8_t*>(first), strlen(first)));
        camera_motion_actions_init();
        assert(camera_motion_actions_to_json(defaults.to<JsonArray>(), &config_reset));
        assert(!config_reset && defaults[0]["args"]["value"] == "first");
    }
    storage_test_fail_open = true;
    camera_motion_actions_init();
    JsonDocument unavailable;
    assert(!camera_motion_actions_to_json(unavailable.to<JsonArray>()));
    storage_test_fail_open = false;
    camera_motion_actions_init();
    assert(camera_motion_actions_save_raw(reinterpret_cast<const uint8_t*>(first), strlen(first)));
    JsonDocument retained;
    assert(camera_motion_actions_to_json(retained.to<JsonArray>()));
    assert(!camera_motion_actions_save_raw(reinterpret_cast<const uint8_t*>(invalid), strlen(invalid)));
    assert(storage_test_files[path] == first);
    const char* legacy = R"({"action_format_version":1,"actions":[{"type":"test_boot"}]})";
    assert(!camera_motion_actions_save_raw(reinterpret_cast<const uint8_t*>(legacy), strlen(legacy)));
    const char* unversioned = R"({"actions":[]})";
    assert(!camera_motion_actions_save_raw(reinterpret_cast<const uint8_t*>(unversioned), strlen(unversioned)));
    assert(storage_test_files[path] == first);
    const char* nul_key = R"({"action_format_version":1,"actions":[],"actions\u0000x":[{"type":"test_boot"}]})";
    assert(!camera_motion_actions_save_raw(reinterpret_cast<const uint8_t*>(nul_key), strlen(nul_key)));
    assert(storage_test_files[path] == first);
    const char* nul_operation = R"({"action_format_version":1,"actions":[{"op":"test.boot\u0000other","args":{"value":"second"}}]})";
    assert(!camera_motion_actions_save_raw(reinterpret_cast<const uint8_t*>(nul_operation), strlen(nul_operation)));
    assert(storage_test_files[path] == first);
    JsonDocument invalid_doc;
    assert(!deserializeJson(invalid_doc, invalid));
    assert(!camera_motion_actions_validate(invalid_doc["actions"].as<JsonArray>()));
    boot_write_fail = true;
    const char* second = R"({"action_format_version":1,"actions":[{"op":"test.boot","args":{"value":"second"}}]})";
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

    const char* readable = R"({"action_format_version":1,"1":{"expire_actions":[]}})";
    storage_test_files["/config/timers.json"] = readable;
    storage_test_fail_open = true;
    timer_config_init();
    JsonDocument unreadable;
    assert(!timer_config_to_json(unreadable.to<JsonObject>()));
    assert(storage_test_files["/config/timers.json"] == readable);
    storage_test_fail_open = false;

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

static void test_legacy_load_is_saveable() {
    storage_test_files["/config/timers.json"] = R"({
        "1":{"mode":"down","countdown":30,"expire_actions":[
            {"type":"test_boot"},{"type":"test_boot"}]},
        "2":{},
        "3":{"expire_actions":[{"type":"test_boot"}]}
    })";
    timer_config_init();
    assert(timer_config_exists());
    JsonDocument document;
    assert(timer_config_to_json(document.to<JsonObject>()));
    assert(document["config_reset"] == true);
    for (uint8_t id = 1; id <= 3; ++id) assert(snapshot(id).count == 0);
    assert(storage_test_files["/config/timers.json"].find("\"type\":\"test_boot\"") != std::string::npos);
    boot_write_fail = true;
    assert(!save(R"({"action_format_version":1,"1":{"expire_actions":[]}})"));
    boot_write_fail = false;
    assert(timer_config_to_json(document.to<JsonObject>()));
    assert(document["config_reset"] == true);
    assert(save(R"({"action_format_version":1,"1":{"expire_actions":[]}})"));
    assert(timer_config_to_json(document.to<JsonObject>()));
    assert(document["config_reset"] == false);
    timer_config_init();
    assert(timer_config_to_json(document.to<JsonObject>()));
    assert(document["config_reset"] == false);
}

static void test_strict_writes_and_atomic_cache() {
    const char* valid = R"({"action_format_version":1,"1":{"expire_actions":[]},"2":{"expire_actions":[{"op":"test.boot","args":{"value":"x"}}]},"3":{"expire_actions":[]}})";
    assert(save(valid));
    assert(snapshot(1).count == 0);
    assert(snapshot(2).count == 1);

    const char* invalid[] = {
        "[]", R"({"0":{"expire_actions":[]}})",
        R"({"action_format_version":1,"":{"expire_actions":[]}})",
        R"({"action_format_version":1,"1\u0000extra":{"expire_actions":[]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[]},"1\u0000extra":{"expire_actions":[{"type":"test_boot"}]}})",
        R"({"action_format_version":1,"4":{"expire_actions":[]}})", R"({"action_format_version":1,"1":[]})",
        R"({"action_format_version":1,"1":{"mode":"up"}})", R"({"action_format_version":1,"1":{"countdown":5}})",
        R"({"action_format_version":1,"1":{"other":[]}})", R"({"action_format_version":1,"1":{"expire_actions":{}}})",
        R"({"action_format_version":1,"1":{"expire_actions":[null]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[{}]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[{"type":"timer","timer_id":1,"timer_command":"start","timer_mode":"down","timer_value":"1234567890123456"}]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[{"op":"test.boot","args":{"value":"1"}},{"op":"test.boot","args":{"value":"2"}},{"op":"test.boot","args":{"value":"3"}},{"op":"test.boot","args":{"value":"4"}}]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[{"type":"future","payload":"keep"}]}})"
    };
    for (const char* json : invalid) {
        std::string before = storage_test_files["/config/timers.json"];
        if (save(json)) {
            std::fprintf(stderr, "accepted invalid timer config: %s\n", json);
            assert(false);
        }
        assert(storage_test_files["/config/timers.json"] == before);
        assert(snapshot(2).count == 1);
    }

    storage_test_write_limit = 5;
    assert(!save(R"({"action_format_version":1,"1":{"expire_actions":[{"op":"test.boot","args":{"value":"x"}}]}})"));
    assert(snapshot(2).count == 1);
    storage_test_write_limit = SIZE_MAX;

    assert(save(R"({"action_format_version":1,"1":{"expire_actions":[{"op":"test.boot","args":{"value":"x"}}]}})"));
    assert(snapshot(1).count == 1);
    assert(snapshot(2).count == 0);
    JsonDocument doc;
    assert(!deserializeJson(doc, storage_test_files["/config/timers.json"]));
    assert(doc["action_format_version"] == 1);
    assert(doc["1"]["expire_actions"][0]["op"] == "test.boot");
    assert(doc["2"]["expire_actions"].as<JsonArray>().size() == 0);
    assert(doc["3"]["expire_actions"].as<JsonArray>().size() == 0);
}

static void test_concurrent_save_and_snapshot() {
    const char* empty = R"({"action_format_version":1,"1":{"expire_actions":[]}})";
    const char* full = R"({"action_format_version":1,"1":{"expire_actions":[{"op":"test.boot","args":{"value":"a"}},{"op":"test.boot","args":{"value":"b"}},{"op":"test.boot","args":{"value":"c"}}]}})";
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
    const char* first = R"({"action_format_version":1,"1":{"expire_actions":[{"op":"test.boot","args":{"value":"first"}}]}})";
    const char* second = R"({"action_format_version":1,"1":{"expire_actions":[{"op":"test.boot","args":{"value":"second"}}]}})";
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
        R"({"1":{"expire_actions":[{"op":"test.boot","args":{"value":"x"}}]}})",
        R"({"action_format_version":2,"1":{"expire_actions":[{"op":"test.boot","args":{"value":"x"}}]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[{"op":"test.boot","args":{"value":true}}]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[{"op":"unknown","args":{}}]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[{"type":"test_boot","op":"test.boot","args":{"value":"x"}}]}})"
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
        R"({"1":{"expire_actions":[{"op":"test.boot","args":{"value":"valid"}}]}})",
        R"({"action_format_version":2,"1":{"expire_actions":[]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[{"op":"test.boot","args":{"value":"valid"}},{"op":"unknown","args":{}}]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[{"op":"test.boot","args":{"value":true}}]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[{"type":"test_boot","op":"test.boot","args":{"value":"x"}}]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[{"type":"test_boot","args":{"value":"x"}}]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[{"type":"future","payload":"preserve"}]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[null]}})",
        R"({"action_format_version":1,"1":{"expire_actions":[{}]}})",
        R"({"action_format_version":1,"1":{"expire_actions":"bad"}})",
        R"({"action_format_version":1,"1":{"mode":"up","expire_actions":[]}})",
        R"({"action_format_version":1,"4":{"expire_actions":[]}})",
        R"({"action_format_version":1,"1":null})",
        R"({"action_format_version":1,"1":{"expire_actions":[{"op":"test.boot","args":{"value":"1"}},{"op":"test.boot","args":{"value":"2"}},{"op":"test.boot","args":{"value":"3"}},{"op":"test.boot","args":{"value":"last"}}]}})"
    };
    for (const char* json : invalid) {
        storage_test_files[path] = json;
        timer_config_init();
        TimerExpirySnapshot result;
        JsonDocument document;
        assert(timer_config_snapshot_expiry(1, &result));
        assert(result.count == 0);
        assert(timer_config_to_json(document.to<JsonObject>()));
        assert(document["config_reset"] == true);
        assert(!save(R"({"1":{"expire_actions":[]}})"));
        assert(storage_test_files[path] == json);
    }
    storage_test_files.erase(path);
    timer_config_init();
    assert(snapshot(1).count == 0);
    assert(save(R"({"action_format_version":1,"1":{"expire_actions":[{"op":"test.boot","args":{"value":"restored"}}]}})"));
    timer_config_init();
    assert(normalized_json().find("restored") != std::string::npos);
    const char* visual = R"({"action_format_version":1,"1":{"expire_actions":[{"type":"visual_alert","op":"start","color":"#ff0000"}]}})";
    assert(!save(visual));
}

static void test_concurrent_saves_stay_consistent() {
    const char* first = R"({"action_format_version":1,"1":{"expire_actions":[{"op":"test.boot","args":{"value":"one"}}]}})";
    const char* second = R"({"action_format_version":1,"1":{"expire_actions":[{"op":"test.boot","args":{"value":"one"}},{"op":"test.boot","args":{"value":"two"}}]}})";
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
    test_legacy_load_is_saveable();
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
