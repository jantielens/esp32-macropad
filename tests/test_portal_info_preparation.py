#!/usr/bin/env python3
import os
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/app/pad_config.cpp").read_text()
info_source = (root / "src/app/web_portal_device_api.cpp").read_text()
pad_api_source = (root / "src/app/web_portal_pad.cpp").read_text()
storage_source = (root / "src/app/storage.cpp").read_text()
icon_source = (root / "src/app/icon_store.cpp").read_text()
icon_header = (root / "src/app/icon_store.h").read_text()
validation_source = (root / "src/app/pad_validate.cpp").read_text()


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
#include <new>
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
static bool ota_active = false;
static bool fail_write = false;
static bool fail_rename = false;
static bool ota_activity_is_active() { return ota_active; }
static int g_file_mutex = 1;
static bool xSemaphoreTake(int, int) { return true; }
static void xSemaphoreGive(int) {}
static const bool pdTRUE = true;
static unsigned esp_random() { static unsigned value = 0; return ++value; }
static unsigned storage_reads = 0;
struct File {
    std::string path;
    bool valid;
    size_t offset = 0;
    explicit operator bool() const { return valid; }
    size_t size() const { return files.at(path).size(); }
    int read() {
        if (offset >= size()) return -1;
        return static_cast<unsigned char>(files.at(path)[offset++]);
    }
    size_t readBytes(char* dst, size_t size) {
        ++storage_reads;
        memcpy(dst, files.at(path).data() + offset, size);
        offset += size;
        return size;
    }
    size_t write(const uint8_t* bytes, size_t size) {
        if (fail_write) return 0;
        files[path].append(reinterpret_cast<const char*>(bytes), size);
        return size;
    }
    bool seek(size_t position) { offset = position; return offset <= size(); }
    void close() {}
};
struct TestStorage {
    File open(const char* path, const char* mode) {
        if (strcmp(mode, "w") == 0) files[path].clear();
        return {path, files.count(path) != 0};
    }
    bool exists(const char* path) { return files.count(path) != 0; }
    bool remove(const char* path) { return files.erase(path) != 0; }
    bool rename(const char* from, const char* to) {
        if (fail_rename) return false;
        const auto found = files.find(from);
        if (found == files.end()) return false;
        files[to] = found->second;
        files.erase(found);
        return true;
    }
} Storage;
static bool storage_mount() { return true; }
static void storage_publish_usage(bool) {}
static std::atomic<bool> g_ready{true};
static void icon_store_preload_pad(uint8_t) {}
enum IconKind : uint8_t { ICON_KIND_COLOR = 0, ICON_KIND_MONO = 1 };
bool icon_store_source_matches_metadata(const char*, const char*, const char*) { return true; }
bool icon_store_artifact_matches(const char*, const char*, IconKind, uint32_t) { return true; }
uint32_t pad_icon_render_key(JsonObjectConst, JsonObjectConst) { return 1; }
void icon_store_build_key(uint8_t, const char*, uint8_t, uint8_t, char* key_out, size_t size) {
    if (size) key_out[0] = '\0';
}
bool icon_store_source_delete_if_unreferenced(const char*) { return true; }
void icon_store_delete_page_icons(uint8_t) {}
void icon_store_cleanup_pad_icons(uint8_t) {}
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
static bool parse_button(JsonObject object, ScreenButtonConfig* button, const ButtonDefaults*) {
    button->col = object["col"] | uint8_t(0);
    button->row = object["row"] | uint8_t(0);
    button->col_span = object["col_span"] | uint8_t(1);
    button->row_span = object["row_span"] | uint8_t(1);
    return true;
}
'''
harness += icon_header[icon_header.index("#ifndef ICON_MAX_DIMENSION"):icon_header.index("// Icon kind")]
harness += icon_header[icon_header.index("struct IconArtifactInfo"):icon_header.index("#ifdef __cplusplus")]
harness += icon_header[icon_header.index("enum IconStoreResult"):icon_header.index("// Reference to a cached icon")]
harness += validation_source[validation_source.index("bool pad_icon_source_id_valid"):validation_source.index("bool pad_icon_catalog_kind_valid")]
harness += icon_source[icon_source.index("static bool validate_png"):icon_source.index("static bool source_matches_metadata_unlocked")]
harness += icon_source[icon_source.index("bool icon_store_source_path"):icon_source.index("bool icon_store_source_matches_kind")]
harness += icon_source[icon_source.index("struct IconSourceUpload {"):icon_source.index("IconStoreResult icon_store_source_install")]
harness += icon_source[icon_source.index("static bool read_artifact_unlocked"):icon_source.index("    static bool artifact_matches_for_preload")]
harness += storage_source[storage_source.index("bool storage_write_verified"):storage_source.index("bool storage_mount")]
harness += section("static bool g_fs_mounted", "// ============================================================================")
harness += section("static void pad_config_path", "// ============================================================================")
harness += section("bool pad_config_init()", "// Internal: read and parse")
harness += section("static int select_template_buttons",
                   "void pad_config_rebuild_all_caches()")
harness += section("void pad_config_rebuild_all_caches()", "bool pad_config_get_data_stream_snapshot")
harness += section("static const char* validate_pad_button_icon", "bool pad_config_save_raw")
harness += section("bool pad_config_save_raw", "bool pad_config_exists")
harness += section("bool pad_config_exists", "uint32_t pad_config_get_generation()")
harness += section("uint32_t pad_config_get_generation()", "uint32_t pad_config_get_eligible_mask()")
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
struct AsyncResponseStream : TestResponse {
    size_t print(const char* text) {
        return write(reinterpret_cast<const uint8_t*>(text), strlen(text));
    }
};
struct AsyncWebServerRequest {
    int page = 0;
    int status = 0;
    std::string body;
    AsyncResponseStream* beginResponseStream(const char*) { return new AsyncResponseStream; }
    void send(AsyncResponseStream* response) {
        status = 200;
        body = response->body;
        delete response;
    }
};
struct WebPortalTiming {
    void attach(AsyncResponseStream*, const char*) {}
};
static bool portal_auth_gate(AsyncWebServerRequest*) { return true; }
static int parse_page_param(AsyncWebServerRequest* request) {
    return request->page >= 0 && request->page < MAX_PADS ? request->page : -1;
}
static void web_portal_send_json_error(AsyncWebServerRequest* request, int status,
                                      const char* message, WebPortalTiming*) {
    request->status = status;
    request->body = message;
}
#define PAD_CONFIG_MAX_JSON_BYTES (48 * 1024)
'''
harness += pad_api_source[pad_api_source.index("void handleGetPadIconContext"):pad_api_source.index("static void preserve_pad_image_passwords")]
harness += r'''
static bool save_document(uint8_t pad, JsonDocument& doc) {
    std::string json;
    serializeJson(doc, json);
    return pad_config_save_raw(pad, reinterpret_cast<const uint8_t*>(json.data()), json.size());
}
static bool save(uint8_t pad, const std::string& name) {
    JsonDocument doc;
    doc["name"] = name;
    doc["buttons"].to<JsonArray>();
    return save_document(pad, doc);
}
static void add_buttons(JsonArray buttons, unsigned count) {
    for (unsigned index = 0; index < count; ++index) {
        JsonObject button = buttons.add<JsonObject>();
        button["col"] = index % 8;
        button["row"] = index / 8;
    }
}
int main() {
    std::string stream_png("\x89PNG\r\n\x1a\n\x00\x00\x00\x0dIHDR\x00\x00\x00\x01\x00\x00\x00\x01", 24);
    stream_png.append(9, '\0');
    stream_png.append("\x00\x01\x00\x00IDAT", 8);
    stream_png.append(65536 + 4, '\0');
    stream_png.append("\x00\x00\x00\x00IEND\x00\x00\x00\x00", 12);
    IconSourceUpload* upload = nullptr;
    assert(icon_store_source_png_begin(ICON_MAX_PNG_SIZE + 1, &upload) == ICON_STORE_TOO_LARGE);
    assert(!upload && files.empty());
    assert(icon_store_source_png_begin(stream_png.size(), &upload) == ICON_STORE_OK);
    const std::string temporary = upload->temporary;
    char retained_id[CONFIG_ICON_SOURCE_ID_MAX_LEN] = {};
    assert(icon_store_source_png_finish(upload, retained_id, sizeof(retained_id)) == ICON_STORE_INVALID);
    for (size_t offset = 0; offset < stream_png.size(); offset += 997) {
        const size_t length = std::min(size_t(997), stream_png.size() - offset);
        assert(icon_store_source_png_write(upload, (const uint8_t*)stream_png.data() + offset, length) == ICON_STORE_OK);
    }
    assert(icon_store_source_png_finish(upload, retained_id, sizeof(retained_id)) == ICON_STORE_OK);
    icon_store_source_png_abort(upload);
    char retained_path[64];
    bool retained_png = false;
    assert(icon_store_source_path(retained_id, retained_path, sizeof(retained_path), &retained_png) && retained_png);
    assert(files.at(retained_path) == stream_png && !files.count(temporary));
    files.clear();
    for (unsigned failure = 0; failure < 5; ++failure) {
        assert(icon_store_source_png_begin(stream_png.size(), &upload) == ICON_STORE_OK);
        fail_write = failure == 0;
        ota_active = failure == 1;
        const IconStoreResult result = icon_store_source_png_write(upload, (const uint8_t*)stream_png.data(), stream_png.size());
        assert(result == (fail_write ? ICON_STORE_IO_ERROR : ota_active ? ICON_STORE_BUSY : ICON_STORE_OK));
        fail_write = ota_active = false;
        if (failure == 2) files[upload->temporary].pop_back();
        if (failure == 3) files[upload->temporary][18] = 3;
        fail_rename = failure == 4;
        assert(icon_store_source_png_finish(upload, retained_id, sizeof(retained_id)) != ICON_STORE_OK);
        fail_rename = false;
        icon_store_source_png_abort(upload);
        assert(files.empty());
    }
    assert(icon_store_source_png_begin(stream_png.size(), &upload) == ICON_STORE_OK);
    icon_store_source_png_abort(upload);
    assert(files.empty());
    const char* icon_source_id = "s_000000000000000000000001";
    JsonDocument metadata;
    metadata["source_id"] = icon_source_id;
    metadata["kind"] = 1;
    metadata["width"] = 90;
    metadata["height"] = 90;
    metadata["render_key"] = 0x4c31e060;
    std::string metadata_json;
    serializeJson(metadata, metadata_json);
    const std::string png("\x89PNG\r\n\x1a\n\x00\x00\x00\x0dIHDR\x00\x00\x00\x5a\x00\x00\x00\x5a", 24);
    files["/icons/pad_0_main_0_0.meta"] = metadata_json;
    files["/icons/pad_0_main_0_0.png"] = png;
    IconArtifactInfo artifact = {};
    assert(read_artifact_unlocked("pad_0_main_0_0", &artifact));
    assert(strcmp(artifact.source_id, icon_source_id) == 0);
    assert(artifact.kind == ICON_KIND_MONO && artifact.width == 90 && artifact.height == 90);
    assert(artifact.render_key == 0x4c31e060);
    assert(artifact_matches_unlocked("pad_0_main_0_0", icon_source_id, ICON_KIND_MONO, 0x4c31e060));
    assert(!artifact_matches_unlocked("pad_0_main_0_0", icon_source_id, ICON_KIND_MONO, 1));
    files["/icons/pad_0_main_0_0.png"][19] = 89;
    assert(!read_artifact_unlocked("pad_0_main_0_0", &artifact));
    files.erase("/icons/pad_0_main_0_0.png");
    assert(!read_artifact_unlocked("pad_0_main_0_0", &artifact));
    files["/icons/pad_0_main_0_0.png"] = png;
    files["/icons/pad_0_main_0_0.meta"] = "{";
    assert(!read_artifact_unlocked("pad_0_main_0_0", &artifact));
    files.erase("/icons/pad_0_main_0_0.meta");
    files.erase("/icons/pad_0_main_0_0.png");
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
    JsonDocument template_pad;
    template_pad["cols"] = 8;
    template_pad["rows"] = 8;
    add_buttons(template_pad["buttons"].to<JsonArray>(), MAX_PAD_BUTTONS - 1);
    assert(save_document(1, template_pad));
    JsonDocument child;
    child["cols"] = 8;
    child["rows"] = 8;
    child["template_pad"] = 1;
    child["buttons"].to<JsonArray>();
    add_buttons(child["overlay_buttons"].to<JsonArray>(), 1);
    assert(save_document(0, child));
    const PadConfig* inherited = pad_config_acquire(0);
    assert(inherited->main_button_count == MAX_PAD_BUTTONS - 1);
    assert(inherited->button_count == MAX_PAD_BUTTONS);
    assert(inherited->buttons[MAX_PAD_BUTTONS - 1].col == 0);
    assert(inherited->buttons[MAX_PAD_BUTTONS - 1].row == 0);
    const std::string old_template = files.at("/config/pad_1.json");
    const std::string old_child = files.at("/config/pad_0.json");
    const uint32_t old_generation = pad_config_get_generation();
    JsonObject extra = template_pad["buttons"].as<JsonArray>().add<JsonObject>();
    extra["col"] = 7;
    extra["row"] = 7;
    assert(!save_document(1, template_pad));
    assert(files.at("/config/pad_1.json") == old_template);
    assert(pad_config_get_generation() == old_generation);
    JsonObject second_overlay = child["overlay_buttons"].as<JsonArray>().add<JsonObject>();
    second_overlay["col"] = 1;
    second_overlay["row"] = 0;
    assert(!save_document(0, child));
    assert(files.at("/config/pad_0.json") == old_child);
    assert(pad_config_get_generation() == old_generation);
    const PadConfig* unchanged = pad_config_acquire(0);
    assert(unchanged == inherited);
    pad_config_release(unchanged);
    pad_config_release(inherited);
    template_pad["buttons"].clear();
    add_buttons(template_pad["buttons"].to<JsonArray>(), 32);
    assert(save_document(1, template_pad));
    child["overlay_buttons"].clear();
    add_buttons(child["overlay_buttons"].to<JsonArray>(), 32);
    assert(save_document(0, child));
    const PadConfig* balanced = pad_config_acquire(0);
    assert(balanced->main_button_count == 32 && balanced->button_count == 64);
    assert(balanced->button_capacity == balanced->button_count);
    for (unsigned index = 0; index < 32; ++index) {
        assert(balanced->buttons[index].col == index % 8);
        assert(balanced->buttons[index].row == index / 8);
        assert(balanced->buttons[index + 32].col == index % 8);
        assert(balanced->buttons[index + 32].row == index / 8);
    }
    pad_config_release(balanced);
    JsonObject local = child["buttons"].as<JsonArray>().add<JsonObject>();
    local["col"] = 0;
    local["row"] = 0;
    local["col_span"] = 2;
    local["row_span"] = 1;
    local["label_top"] = "Private label";
    local["bg_image_password"] = "Secret password";
    child["column_widths_px"].add(40);
    child["row_heights_px"].add(50);
    assert(save_document(0, child));
    AsyncWebServerRequest context_request;
    context_request.page = 1;
    const unsigned context_reads = storage_reads;
    handleGetPadIconContext(&context_request);
    assert(context_request.status == 200);
    assert(storage_reads == context_reads + 1);
    JsonDocument context;
    assert(!deserializeJson(context, context_request.body));
    assert(context["dependents"].size() == 1);
    JsonObject dependent = context["dependents"][0];
    assert(dependent["page"] == 0);
    assert(dependent["cols"] == 8 && dependent["rows"] == 8);
    assert(dependent["button_count"] == 33);
    assert(dependent["column_widths_px"][0] == 40);
    assert(dependent["row_heights_px"][0] == 50);
    assert(dependent["buttons"].size() == 1);
    assert(dependent["buttons"][0]["col_span"] == 2);
    assert(dependent["buttons"][0].size() == 4);
    assert(dependent["overlay_buttons"].isNull());
    assert(context_request.body.find("Private label") == std::string::npos);
    assert(context_request.body.find("Secret password") == std::string::npos);
    context_request.page = MAX_PADS - 1;
    handleGetPadIconContext(&context_request);
    assert(context_request.status == 200 && context_request.body == "{\"dependents\":[]}");
    assert(storage_reads == context_reads + 1);
    context_request.page = -1;
    handleGetPadIconContext(&context_request);
    assert(context_request.status == 400);
    context_request.page = 1;
    fail_allocation = true;
    handleGetPadIconContext(&context_request);
    fail_allocation = false;
    assert(context_request.status == 503);
    const std::string saved_child = files.at("/config/pad_0.json");
    files["/config/pad_0.json"] = "{";
    handleGetPadIconContext(&context_request);
    assert(context_request.status == 503);
    files["/config/pad_0.json"] = saved_child;
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