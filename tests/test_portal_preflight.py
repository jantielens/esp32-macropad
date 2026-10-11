#!/usr/bin/env python3
"""Check production core preflight matching and CORS responses with C++11."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PRELUDE = r'''
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
using String = std::string;
static const unsigned HTTP_OPTIONS = 7;
struct AsyncWebServerResponse {
    unsigned status;
    std::vector<std::string> headers;
    void addHeader(const char* name, const char* value) {
        headers.emplace_back(std::string(name) + ":" + value);
    }
};
struct AsyncWebServerRequest {
    unsigned verb;
    String path;
    AsyncWebServerResponse response;
    bool sent = false;
    AsyncWebServerRequest(unsigned method, const char* url) : verb(method), path(url) {}
    unsigned method() const { return verb; }
    const String& url() const { return path; }
    AsyncWebServerResponse* beginResponse(unsigned status) {
        response.status = status;
        return &response;
    }
    void send(AsyncWebServerResponse* target) { assert(target == &response); sent = true; }
};
struct AsyncWebHandler {
    virtual ~AsyncWebHandler() {}
    virtual bool canHandle(AsyncWebServerRequest*) const { return false; }
    virtual void handleRequest(AsyncWebServerRequest*) {}
};
static const char* cors_origin = "https://example.github.io";
const char* web_portal_cors_origin() { return cors_origin; }
void web_portal_send_cors_preflight(AsyncWebServerRequest*);
'''
CHECKS = r'''
int main() {
    CorePreflightHandler handler;
    assert(!handler.canHandle(nullptr));
    const std::vector<std::string> expected = {
        "/api/config", "/api/info", "/api/bindings",
#if HEALTH_HISTORY_ENABLED
        "/api/health/history",
#endif
        "/api/health", "/api/reboot", "/api/firmware/update/status", "/api/firmware/update",
#if HAS_DISPLAY
        "/api/recipes/catalog", "/api/pad/button_sizes", "/api/pad/blocks",
#if HAS_MQTT
        "/api/pad/resolve",
#endif
        "/api/pad", "/api/screenshot",
#if HAS_TOUCH
        "/api/screen/tap",
#endif
        "/api/icons/install", "/api/icons/page", "/api/icons/installed",
        "/api/icons/files", "/api/icons/cache", "/api/icons/file",
#endif
#if HAS_CAMERA
        "/api/camera/snapshot.raw", "/api/camera/snapshot.jpg",
#endif
        "/api/update",
#if HAS_BLE_HID
        "/api/ble/pairing/start",
#endif
#if HAS_TTF_FONTS
        "/api/fonts/list", "/api/fonts/upload", "/api/fonts",
#endif
#if HAS_SOUND_PLAYER
        "/api/sounds/upload", "/api/sounds/list", "/api/sounds", "/api/sounds/play",
#endif
    };
    assert((std::vector<std::string>(std::begin(core_preflight_paths),
                                    std::end(core_preflight_paths)) == expected));
    for (const auto& path : expected) {
        for (const auto& suffix : {"", "/child"}) {
            const std::string url = path + suffix;
            AsyncWebServerRequest request(HTTP_OPTIONS, url.c_str());
            assert(handler.canHandle(&request));
        }
        for (unsigned method = 0; method < HTTP_OPTIONS; ++method) {
            AsyncWebServerRequest request(method, path.c_str());
            assert(!handler.canHandle(&request));
        }
    }
    for (const auto* url : {"", "/", "/api", "/api/unknown", "/api/configuration",
                            "/api/info-extra", "/api/firmware/updater", "/API/config"}) {
        AsyncWebServerRequest request(HTTP_OPTIONS, url);
        assert(!handler.canHandle(&request));
    }
    AsyncWebServerRequest history(HTTP_OPTIONS, "/api/health/history");
    assert(handler.canHandle(&history));
    AsyncWebServerRequest resolve(HTTP_OPTIONS, "/api/pad/resolve");
    assert(handler.canHandle(&resolve) == bool(HAS_DISPLAY));
    AsyncWebServerRequest request(HTTP_OPTIONS, "/api/config");
    handler.handleRequest(&request);
    assert(request.sent && request.response.status == 204);
    assert((request.response.headers == std::vector<std::string>{
        "Access-Control-Allow-Origin:https://example.github.io",
        "Access-Control-Allow-Headers:Authorization, Content-Type",
        "Access-Control-Allow-Methods:GET,POST,PUT,DELETE,OPTIONS", "Vary:Origin"}));
    cors_origin = "";
    AsyncWebServerRequest no_origin(HTTP_OPTIONS, "/api/config");
    handler.handleRequest(&no_origin);
    assert(no_origin.sent && no_origin.response.status == 204 && no_origin.response.headers.empty());
}
'''


def main():
    source = (ROOT / "src/app/web_portal_device_api.cpp").read_text()
    probe = source[source.index("static bool fill_requested_task_stack("):
                   source.index("// GET /api/health -")]
    health = source
    health = health[health.index("void handleGetHealth("):health.index("void handleGetHealthHistory(")]
    start_probe = health.index("fill_requested_task_stack(request, *doc)")
    assert "if (!portal_auth_gate(request)) return;" in health
    assert health.index("portal_auth_gate(request)") < start_probe
    audio = (ROOT / "src/app/audio.cpp").read_text()
    getter = audio[audio.index("bool audio_get_stack_watermark("):
                   audio.index("void audio_set_volume(")]
    handle = next(line for line in audio.splitlines()
                  if line.startswith("static std::atomic<TaskHandle_t> audio_task_handle"))
    assert "vTaskDelete" not in audio
    assert "audio_task_handle.store(created_task, std::memory_order_release)" in audio
    assert "xTaskGetHandle" not in probe
    for path in ROOT.glob("src/app/web_portal*.cpp"):
        if path.name != "web_portal_device_api.cpp":
            assert "uxTaskGetStackHighWaterMark" not in path.read_text(), path
    with tempfile.TemporaryDirectory(prefix="portal-stack-") as directory:
        test = pathlib.Path(directory) / "test.cpp"
        binary = pathlib.Path(directory) / "test"
        test.write_text(r'''
#include <cassert>
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
using String = std::string;
using TaskHandle_t = void*;
static unsigned tcp_scans = 0;
static unsigned audio_scans = 0;
static TaskHandle_t test_audio_task = reinterpret_cast<void*>(1);
unsigned uxTaskGetStackHighWaterMark(void* task) {
    if (task == nullptr) { ++tcp_scans; return 3400; }
    assert(task == test_audio_task);
    ++audio_scans;
    return 21704;
}
const char* pcTaskGetName(void*) { return "async_tcp"; }
struct JsonObject {
    std::map<std::string, std::string>* values;
    struct Field {
        std::map<std::string, std::string>* values;
        std::string key;
        void operator=(const char* value) { (*values)[key] = value; }
        void operator=(uint32_t value) { (*values)[key] = std::to_string(value); }
    };
    Field operator[](const char* key) { return {values, key}; }
};
struct JsonDocument {
    std::map<std::string, std::map<std::string, std::string>> values;
    struct Field {
        std::map<std::string, std::string>* values;
        template<typename Value> Value to() { return {values}; }
    };
    Field operator[](const char* key) { return {&values[key]}; }
};
struct AsyncWebServerRequest {
    std::map<std::string, std::string> params;
    struct Param {
        std::string parameter;
        const std::string& value() const { return parameter; }
    } active;
    int status = 200;
    bool hasParam(const char* key) const { return params.count(key) != 0; }
    Param* getParam(const char* key) { active.parameter = params.at(key); return &active; }
    void send(int code, const char*, const char*) { status = code; }
};
''' + "#if HAS_AUDIO\n" + handle + "\n" + getter + "\n#endif\n" + probe + r'''
int main() {
    JsonDocument doc;
    AsyncWebServerRequest request;
    assert(fill_requested_task_stack(&request, doc));
    assert(tcp_scans == 0 && audio_scans == 0 && doc.values.empty());
    for (const auto* value : {"", "0", "true", "2"}) {
        request.params["async_tcp_stack"] = value;
        assert(fill_requested_task_stack(&request, doc));
        assert(tcp_scans == 0 && audio_scans == 0 && doc.values.empty());
    }
    request.params["async_tcp_stack"] = "1";
    assert(fill_requested_task_stack(&request, doc));
    assert(tcp_scans == 1);
    assert(doc.values["async_tcp_stack"]["minimum_free_bytes"] == "3400");
    doc.values.clear();
    request.params["task"] = "async_tcp";
    assert(fill_requested_task_stack(&request, doc));
    assert(tcp_scans == 2 && audio_scans == 0);
    assert(doc.values["task_stack"]["task"] == "async_tcp");
    assert(doc.values.count("async_tcp_stack") == 0);
#ifdef CONFIG_ASYNC_TCP_STACK_SIZE
    assert(doc.values["task_stack"]["configured_bytes"] == "8192");
#else
    assert(doc.values["task_stack"].count("configured_bytes") == 0);
#endif
    for (const auto* value : {"", "XXX", "audio_stack", "loopTask"}) {
        request.params["task"] = value;
        assert(!fill_requested_task_stack(&request, doc));
        assert(request.status == (request.params["task"].empty() ? 400 : 404));
        assert(tcp_scans == 2 && audio_scans == 0);
    }
    request.params["task"] = "audio";
    assert(!fill_requested_task_stack(&request, doc));
    assert(request.status == 503 && tcp_scans == 2 && audio_scans == 0);
#if HAS_AUDIO
    assert(!audio_get_stack_watermark(nullptr));
    audio_task_handle.store(test_audio_task, std::memory_order_release);
    assert(!audio_get_stack_watermark(nullptr));
    request.status = 200;
    assert(fill_requested_task_stack(&request, doc));
    assert(request.status == 200 && audio_scans == 1 && tcp_scans == 2);
    assert(doc.values["task_stack"]["task"] == "audio");
    assert(doc.values["task_stack"]["minimum_free_bytes"] == "21704");
    assert(doc.values["task_stack"]["configured_bytes"] == std::to_string(AUDIO_TASK_STACK_SIZE));
#endif
}
''')
        for definitions in (["-DHAS_AUDIO=0"], ["-DHAS_AUDIO=1", "-DAUDIO_TASK_STACK_SIZE=24576"],
                            ["-DHAS_AUDIO=1", "-DAUDIO_TASK_STACK_SIZE=24576",
                             "-DCONFIG_ASYNC_TCP_STACK_SIZE=8192"],
                            ["-DHAS_AUDIO=1", "-DAUDIO_TASK_STACK_SIZE=8192",
                             "-DCONFIG_ASYNC_TCP_STACK_SIZE=8192"]):
            subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                            *definitions, str(test), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    print("Portal stack scans are query-only and follow the authentication gate")
    routes = (ROOT / "src/app/web_portal_routes.cpp").read_text()
    start = routes.index("static const char* const core_preflight_paths")
    end = routes.index("void web_portal_register_routes", start)
    cors = (ROOT / "src/app/web_portal_cors.cpp").read_text()
    constants = cors[cors.index("static constexpr const char kCorsAllowHeaders"):
                     cors.index("void web_portal_add_default_cors_headers")]
    responses = cors[cors.index("void web_portal_add_cors_headers"):]
    flags = ["HEALTH_HISTORY_ENABLED", "HAS_DISPLAY", "HAS_MQTT", "HAS_TOUCH",
             "HAS_CAMERA", "HAS_BLE_HID", "HAS_TTF_FONTS", "HAS_SOUND_PLAYER"]
    profiles = [dict.fromkeys(flags, 0), dict.fromkeys(flags, 1)]
    profiles += [dict(dict.fromkeys(flags, 1), **{flag: 0}) for flag in flags]
    with tempfile.TemporaryDirectory(prefix="portal-preflight-") as directory:
        test = pathlib.Path(directory) / "test.cpp"
        binary = pathlib.Path(directory) / "test"
        test.write_text(PRELUDE + routes[start:end] + constants + responses + CHECKS)
        for profile in profiles:
            subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                            *[f"-D{flag}={value}" for flag, value in profile.items()],
                            str(test), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    print("Portal preflight matching and CORS checks passed (10 profiles)")


if __name__ == "__main__":
    main()