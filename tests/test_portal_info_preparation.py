#!/usr/bin/env python3
import os
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/app/pad_config.cpp").read_text()
info_source = (root / "src/app/web_portal_device_api.cpp").read_text()


def section(start, end):
    offset = source.index(start)
    return source[offset:source.index(end, offset)]


harness = r'''
#include "pad_config.h"
#include "esp_heap_caps.h"
#include "psram_json_allocator.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
using std::min;
static bool fail_allocation = false;
void* heap_caps_malloc(size_t size, uint32_t) {
    return fail_allocation ? nullptr : malloc(size);
}
void heap_caps_free(void* pointer) { free(pointer); }
bool psramFound() { return false; }
#define LOGD(...) ((void)0)
#define LOGT(...) ((void)0)
#define LOGI(...) ((void)0)
#define LOGW(...) ((void)0)
#define LOGE(...) ((void)0)
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(mutex) (mutex)->lock()
#define portEXIT_CRITICAL(mutex) (mutex)->unlock()
static size_t test_strlcpy(char* dst, const char* src, size_t capacity) {
    const size_t size = strlen(src);
    if (capacity) {
        const size_t copied = std::min(size, capacity - 1);
        memcpy(dst, src, copied);
        dst[copied] = '\0';
    }
    return size;
}
#define strlcpy test_strlcpy
static std::map<std::string, std::string> files;
static unsigned storage_reads = 0;
struct File {
    std::string path;
    bool valid;
    explicit operator bool() const { return valid; }
    size_t size() const { return files.at(path).size(); }
    size_t readBytes(char* dst, size_t size) {
        ++storage_reads;
        memcpy(dst, files.at(path).data(), size);
        return size;
    }
    size_t write(const uint8_t* bytes, size_t size) {
        files[path].assign(reinterpret_cast<const char*>(bytes), size);
        return size;
    }
    void close() {}
};
struct TestStorage {
    File open(const char* path, const char* mode) {
        if (strcmp(mode, "w") == 0) files[path].clear();
        return {path, files.count(path) != 0};
    }
    bool exists(const char* path) { return files.count(path) != 0; }
    bool remove(const char* path) { return files.erase(path) != 0; }
} Storage;
static bool storage_mount() { return true; }
static void storage_publish_usage(bool) {}
static void icon_store_preload_pad(uint8_t) {}
static ButtonDefaults defaults = {};
static const ButtonDefaults* button_defaults_get() { return &defaults; }
static const char* btn_default(const char* value, const char* fallback) {
    return value[0] ? value : fallback;
}
static void parse_bindable_field(JsonVariantConst value, char* out,
                                 size_t capacity, const char* fallback) {
    strlcpy(out, value | fallback, capacity);
}
static ButtonShadowMode parse_button_shadow_mode(JsonVariantConst) {
    return BUTTON_SHADOW_INHERIT;
}
static bool is_valid_binding_name(const char*) { return true; }
static uint8_t action_list_parse(JsonVariantConst, ButtonAction*, size_t, bool) {
    return 0;
}
static void parse_button(JsonObject, ScreenButtonConfig*, const ButtonDefaults*) {}
static void merge_template_buttons(uint8_t, PadConfig**) {}
'''
harness += section("static bool g_fs_mounted", "// ============================================================================")
harness += section("static void pad_config_path", "// ============================================================================")
harness += section("bool pad_config_init()", "// Internal: read and parse")
harness += section("static PadConfig* pad_config_load_from_flash(uint8_t page, bool skip_template)",
                   "void pad_config_rebuild_all_caches()")
harness += section("void pad_config_rebuild_all_caches()", "bool pad_config_get_data_stream_snapshot")
harness += section("bool pad_config_save_raw", "bool pad_config_exists")
harness += section("bool pad_config_read_name", "int pad_config_resolve_ref")
harness += info_source[info_source.index("template <typename TJson"):info_source.index("static void print_json_string")]
harness += r'''
struct TestResponse {
    std::string body;
    unsigned writes = 0;
    bool short_write = false;
    size_t write(const uint8_t* bytes, size_t size) {
        ++writes;
        const size_t written = short_write && size ? size - 1 : size;
        body.append(reinterpret_cast<const char*>(bytes), written);
        return written;
    }
    size_t write(uint8_t byte) { return write(&byte, 1); }
};
static bool save(uint8_t pad, const std::string& name) {
    JsonDocument doc;
    doc["name"] = name;
    doc["buttons"].to<JsonArray>();
    std::string json;
    serializeJson(doc, json);
    return pad_config_save_raw(pad, reinterpret_cast<const uint8_t*>(json.data()), json.size());
}
int main() {
    files["/config/pad_0.json"] = R"({"name":"Boot name","buttons":[]})";
    assert(pad_config_init());
    char copied[64];
    const unsigned reads = storage_reads;
    assert(pad_config_read_name(0, copied, sizeof(copied)));
    assert(strcmp(copied, "Boot name") == 0);
    assert(storage_reads == reads);
    const PadConfig* old = pad_config_acquire(0);
    assert(save(0, "Renamed"));
    assert(strcmp(old->name, "Boot name") == 0);
    assert(pad_config_read_name(0, copied, sizeof(copied)));
    assert(strcmp(copied, "Renamed") == 0);
    pad_config_release(old);
    assert(save(0, ""));
    assert(!pad_config_read_name(0, copied, sizeof(copied)) && copied[0] == '\0');
    const std::string full_name = std::string(1024, 'x') + "\"\\\n";
    assert(save(0, full_name));
    const PadConfig* full = pad_config_acquire(0);
    assert(full_name == full->name);
    char bounded[4];
    assert(pad_config_read_name(0, bounded, sizeof(bounded)));
    assert(strcmp(bounded, "xxx") == 0);
    assert(pad_config_delete(0));
    assert(pad_config_acquire(0) == nullptr);
    assert(full_name == full->name);
    pad_config_release(full);
    assert(!pad_config_read_name(0, copied, sizeof(copied)));
    assert(!pad_config_read_name(MAX_PADS, copied, sizeof(copied)));
    assert(!pad_config_read_name(0, nullptr, 0));
    PadConfig* resized = pad_config_create(1, full_name.c_str());
    resized->button_count = 1;
    resized->buttons[0].row = 2;
    assert(pad_config_resize(&resized, 4));
    assert(full_name == resized->name);
    assert(resized->buttons[0].row == 2);
    assert(resized->name == reinterpret_cast<char*>(resized->buttons + 4));
    assert(!pad_config_resize(&resized, 0));
    pad_config_release(resized);
    files["/config/pad_1.json"] = R"({"name":"Imported","buttons":[]})";
    pad_config_rebuild_all_caches();
    assert(pad_config_read_name(1, copied, sizeof(copied)));
    assert(strcmp(copied, "Imported") == 0);
    cache_replace(2, pad_config_create(0, "A"));
    std::thread writer([] {
        for (unsigned iteration = 0; iteration < 2000; ++iteration) {
            cache_replace(2, pad_config_create(0, iteration % 2 ? "A" : "B"));
        }
    });
    for (unsigned iteration = 0; iteration < 2000; ++iteration) {
        const PadConfig* snapshot = pad_config_acquire(2);
        assert(strcmp(snapshot->name, "A") == 0 || strcmp(snapshot->name, "B") == 0);
        pad_config_release(snapshot);
    }
    writer.join();
    for (uint8_t pad = 0; pad < MAX_PADS; ++pad) cache_replace(pad, nullptr);
    JsonDocument catalog;
    JsonArray entries = catalog.to<JsonArray>();
    JsonObject entry = entries.add<JsonObject>();
    entry["type"] = "test";
    entry["label"] = full_name;
    entry["available"] = true;
    std::string expected;
    serializeJson(entries, expected);
    TestResponse buffered;
    assert(write_json_buffered(entries, buffered));
    assert(buffered.body == expected && buffered.writes == 1);
    TestResponse fallback;
    fail_allocation = true;
    assert(write_json_buffered(entries, fallback));
    fail_allocation = false;
    assert(fallback.body == expected && fallback.writes > 1);
    TestResponse short_response;
    short_response.short_write = true;
    assert(!write_json_buffered(entries, short_response));
    entry["available"] = false;
    TestResponse fresh;
    assert(write_json_buffered(entries, fresh));
    assert(fresh.body != expected);
    entries.clear();
    TestResponse empty;
    assert(write_json_buffered(entries, empty) && empty.body == "[]");
    puts("portal_info_preparation: PASS");
}
'''

info_handler = info_source[info_source.index("void handleGetVersion"):info_source.index("void handleGetBindings")]
assert "pad_config_read_raw(" not in info_handler
assert "pad_config_exists(" not in info_handler
assert "pad_config_acquire(" in info_handler
assert "pad_config_release(" in info_handler

with tempfile.TemporaryDirectory() as tmp:
    harness_path = pathlib.Path(tmp) / "portal_info_preparation.cpp"
    executable = pathlib.Path(tmp) / "portal_info_preparation"
    harness_path.write_text(harness)
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-Wno-deprecated-declarations", "-pthread", "-DTEST_HEAP_CAPS_ALLOCATOR",
        "-include", str(root / "tests/Arduino.h"),
        "-include", str(root / "tests/board_config.h"),
        "-I", str(root / "tests"), "-I", str(root / "src/app"),
        "-I", str(pathlib.Path.home() / "Arduino/libraries/ArduinoJson/src"),
        str(harness_path), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)