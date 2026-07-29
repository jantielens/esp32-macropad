#include <ArduinoJson.h>
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <thread>
#include <freertos/semphr.h>

#include "timer_config.h"
#include <LittleFS.h>

std::map<std::string, std::string> timer_test_files;
bool timer_test_fail_open = false;
size_t timer_test_write_limit = SIZE_MAX;
FakeLittleFS LittleFS;

void storage_publish_usage(bool) {}

static std::string normalized_json() {
    JsonDocument doc;
    timer_config_to_json(doc.to<JsonObject>());
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
    timer_test_files.clear();
    timer_config_init();
    assert(!timer_config_exists());
    assert(snapshot(1).count == 0);
    assert(snapshot(2).count == 0);
    assert(snapshot(3).count == 0);

    timer_test_files["/config/timers.json"] = "{malformed";
    timer_config_init();
    assert(timer_config_exists());
    assert(snapshot(1).count == 0);
    assert(snapshot(2).count == 0);
    assert(snapshot(3).count == 0);
}

static void test_tolerant_load_and_normalization() {
    timer_test_files["/config/timers.json"] = R"({
        "1":{"mode":"down","countdown":30,"expire_actions":[
            {"type":"beep","beep_pattern":"a"},null,{},
            {"type":"unknown"},{"type":"sound","sound_file":"alarm"},
            {"type":"mqtt","topic":"ignored"}]},
        "2":{"expire_actions":"bad"},
        "3":{"expire_actions":[{"type":"beep"}]}
    })";
    timer_config_init();
    assert(timer_config_exists());
    assert(snapshot(1).count == 3);
    assert(snapshot(2).count == 0);
    assert(snapshot(3).count == 1);

    JsonDocument doc;
    assert(!deserializeJson(doc, normalized_json()));
    JsonObject root = doc.as<JsonObject>();
    assert(root.size() == 3);
    assert(root["1"]["expire_actions"].as<JsonArray>().size() == 3);
    assert(!root["1"].as<JsonObject>().containsKey("mode"));
    assert(!root["1"].as<JsonObject>().containsKey("countdown"));
}

static void test_strict_writes_and_atomic_cache() {
    const char* valid = R"({"1":{"expire_actions":[]},"2":{"expire_actions":[{"type":"beep"}]},"3":{"expire_actions":[]}})";
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
        R"({"1":{"expire_actions":[{"type":"a"},{"type":"b"},{"type":"c"},{"type":"d"}]}})"
    };
    for (const char* json : invalid) {
        std::string before = timer_test_files["/config/timers.json"];
        assert(!save(json));
        assert(timer_test_files["/config/timers.json"] == before);
        assert(snapshot(2).count == 1);
    }

    timer_test_write_limit = 5;
    assert(!save(R"({"1":{"expire_actions":[{"type":"sound"}]}})"));
    assert(snapshot(2).count == 1);
    timer_test_write_limit = SIZE_MAX;

    assert(save(R"({"1":{"expire_actions":[{"type":"future"}]}})"));
    assert(snapshot(1).count == 1);
    assert(snapshot(2).count == 0);
    JsonDocument doc;
    assert(!deserializeJson(doc, timer_test_files["/config/timers.json"]));
    assert(doc["1"]["expire_actions"][0]["type"] == "future");
    assert(doc["2"]["expire_actions"].as<JsonArray>().size() == 0);
    assert(doc["3"]["expire_actions"].as<JsonArray>().size() == 0);
}

static void test_concurrent_save_and_snapshot() {
    const char* empty = R"({"1":{"expire_actions":[]}})";
    const char* full = R"({"1":{"expire_actions":[{"type":"a"},{"type":"b"},{"type":"c"}]}})";
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

static void test_concurrent_saves_stay_consistent() {
    const char* first = R"({"1":{"expire_actions":[{"type":"first"}]}})";
    const char* second = R"({"1":{"expire_actions":[{"type":"second"},{"type":"second"}]}})";
    std::thread first_writer([&]() { assert(save(first)); });
    std::thread second_writer([&]() { assert(save(second)); });
    first_writer.join();
    second_writer.join();

    JsonDocument persisted;
    assert(!deserializeJson(persisted, timer_test_files["/config/timers.json"]));
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
    test_tolerant_load_and_normalization();
    test_strict_writes_and_atomic_cache();
    test_concurrent_save_and_snapshot();
    test_concurrent_saves_stay_consistent();
    std::puts("timer_config: PASS");
    return 0;
}
