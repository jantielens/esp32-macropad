#!/usr/bin/env python3
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class RemoteLogRuntimeTests(unittest.TestCase):
    def test_board_feature_policy(self):
        config = (ROOT / "config.sh").read_text()
        boards = re.findall(r'^\s*\["([^"]+)"\]="([^"]+)"', config, re.MULTILINE)
        self.assertTrue(boards)
        for board, fqbn in boards:
            flash = re.search(r"FlashSize=(\d+)M", fqbn)
            enabled = ":esp32p4:" in fqbn or (
                ":esp32s3:" in fqbn and flash is not None and int(flash[1]) > 5)
            for override in (None, 0):
                with self.subTest(board=board, override=override):
                    source = f'''#include "board_config.h"
#if HAS_REMOTE_LOG != {int(enabled) if override is None else override}
#error Unexpected remote log configuration
#endif
'''
                    flags = [] if override is None else [f"-DHAS_REMOTE_LOG={override}"]
                    subprocess.run(["c++", "-E", "-x", "c++", "-DBOARD_HAS_OVERRIDE",
                                    "-DBOARD_HAS_PSRAM", *flags, "-I", str(ROOT / "src/app"),
                                    "-I", str(ROOT / "src/boards" / board), "-"],
                                   input=source, text=True, stdout=subprocess.DEVNULL, check=True)

    def test_feature_exclusion(self):
        surfaces = {
            "web_portal_routes.cpp": ("/api/logs", "/portal-logs.js", "/api/debug/crash"),
            "web_portal_pages.cpp": ("handlePortalLogsJS", "portal_logs_full_mode_enabled"),
            "web_portal_component_api.cpp": ("portal_logs_full_mode_enabled",),
            "portal_components.cpp": ("logs_component.cpp",),
            "app.ino": ("remote_log_finish_boot", "Firmware ELF SHA256", "Reset reason:"),
            "web_assets.h": ("logs_fragment_html_gz", "portal_logs_js_gz"),
        }
        with tempfile.TemporaryDirectory() as directory:
            project = pathlib.Path(directory)
            tools = project / "tools"
            tools.mkdir()
            for tool in (ROOT / "tools").iterdir():
                (tools / tool.name).symlink_to(tool, target_is_directory=tool.is_dir())
            app = project / "src/app"
            app.mkdir(parents=True)
            for name in ("web", "device_classes"):
                shutil.copytree(ROOT / "src/app" / name, app / name)
            (project / "src/version.h").symlink_to(ROOT / "src/version.h")
            result = subprocess.run(["bash", str(tools / "minify-web-assets.sh")],
                                    cwd=project, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True)
            self.assertEqual(result.returncode, 0, result.stdout)
            generated_assets = (app / "web_assets.h").read_text()
        for filename, markers in surfaces.items():
            source = generated_assets if filename == "web_assets.h" else (
                ROOT / "src/app" / filename).read_text()
            if filename == "portal_components.cpp":
                source = source.replace('#include "components/logs_component.cpp"', '"logs_component.cpp"')
            source = re.sub(r'^\s*#include[^\n]*', '', source, flags=re.MULTILINE)
            for enabled in (0, 1):
                with self.subTest(filename=filename, enabled=enabled):
                    output = subprocess.check_output(
                        ["c++", "-E", "-P", "-x", "c++", f"-DHAS_REMOTE_LOG={enabled}",
                         "-DDEBUG_CRASH_API_ENABLED=1", "-DBOARD_HAS_PSRAM", "-I", str(ROOT / "src/app"), "-"],
                        input='#include "board_config.h"\n' + source, text=True)
                    for marker in markers:
                        self.assertEqual(marker in output, bool(enabled), marker)

    def test_log_route_dispatch(self):
        source = (ROOT / "src/app/web_portal_routes.cpp").read_text()
        routes = re.findall(r'server->on\("(/api/logs[^\"]*)", HTTP_GET, (\w+)\)', source)
        for path, expected in (("/api/logs", "handleGetLogs"),
                               ("/api/logs/crash", "handleGetCrashLog"),
                               ("/api/logs/crash/download", "handleDownloadCrashLog")):
            with self.subTest(path=path):
                selected = next((handler for prefix, handler in routes
                                 if path == prefix or path.startswith(prefix + "/")), None)
                self.assertEqual(selected, expected)

    def test_debug_crash_gating(self):
        defaults = (ROOT / "src/app/board_config.h").read_text()
        self.assertIn("#define DEBUG_CRASH_API_ENABLED 0", defaults)
        enabled_boards = [path.parent.name for path in (ROOT / "src/boards").glob("*/board_overrides.h")
                          if "#define DEBUG_CRASH_API_ENABLED 1" in path.read_text()]
        self.assertEqual(enabled_boards, [])
        for board, enabled in (("esp32-p4-lcd4b", 0), ("esp32-p4-lcd4b-voice", 0),
                       ("jc3636w518", 0), ("jc3636w518-sd", 0),
                       ("esp32-p4-lcd4b", 1), ("jc3636w518", 1), ("jc3636w518-sd", 1)):
            source = f'''#include "{ROOT / "src/app/board_config.h"}"
#if DEBUG_CRASH_API_ENABLED != {enabled}
#error Unexpected crash injection configuration
#endif
'''
            opt_in = ["-DDEBUG_CRASH_API_ENABLED=1"] if enabled else []
            subprocess.run(["c++", "-E", "-x", "c++", "-DBOARD_HAS_OVERRIDE", "-DBOARD_HAS_PSRAM", *opt_in,
                            "-I", str(ROOT / "src/boards" / board), "-"],
                           input=source, text=True, stdout=subprocess.DEVNULL, check=True)
        routes = (ROOT / "src/app/web_portal_routes.cpp").read_text()
        gated_routes = routes.split("#if DEBUG_CRASH_API_ENABLED", 1)[1].split("#endif", 1)[0]
        self.assertIn('server->on("/api/debug/crash", HTTP_POST, handleDebugCrash)', gated_routes)

    def test_production_capture(self):
        files = {
            "board_config.h": "#pragma once\n#define REMOTE_LOG_BOOT_RECORDS 2\n",
            "esp_attr.h": "#pragma once\n#define DRAM_ATTR __attribute__((section(\".dram1\")))\n",
            "esp_idf_version.h": r'''
#pragma once
#define ESP_IDF_VERSION_VAL(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(ESP_IDF_VERSION_MAJOR, 5, 2)
''',
            "esp_partition.h": r'''
#pragma once
#include <algorithm>
#include <cstring>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_NOT_FOUND = 1, ESP_ERR_INVALID_CRC = 2;
constexpr int ESP_PARTITION_TYPE_DATA = 1, ESP_PARTITION_SUBTYPE_DATA_COREDUMP = 3;
struct esp_partition_t { size_t address = 4096; size_t size = 4096; };
inline esp_partition_t dump_partition;
inline bool partition_present = true, read_failure = false;
inline size_t max_read = 0;
inline const esp_partition_t* esp_partition_find_first(int, int, const char*) {
    return partition_present ? &dump_partition : nullptr;
}
inline esp_err_t esp_partition_read(const esp_partition_t* partition, size_t offset, void* data, size_t size) {
    assert(partition && offset + size <= partition->size && size <= 1024);
    max_read = std::max(max_read, size);
    if (read_failure) return ESP_ERR_INVALID_CRC;
    for (size_t index = 0; index < size; ++index) static_cast<uint8_t*>(data)[index] = (offset + index) % 256;
    return ESP_OK;
}
''',
            "esp_core_dump.h": r'''
#pragma once
#include "esp_partition.h"
#include "esp_idf_version.h"
inline esp_err_t dump_result = ESP_OK;
inline size_t dump_address = 4096, dump_size = 2051;
struct esp_core_dump_summary_t {
    uint32_t exc_pc = 0;
    char exc_task[16] = {};
    uint8_t app_elf_sha256[65] = {};
    struct {
        #if CONFIG_IDF_TARGET_ARCH_RISCV
        uint32_t mcause = 7, mtval = 0x500d2000, ra = 0x400cfc8c, sp = 0x4ff41350;
        uint32_t mstatus = 0x1880, mtvec = 0x4ff00003, exc_a[8] = {};
        #else
        uint32_t exc_cause = 29, exc_vaddr = 0x500d2000, exc_a[16] = {0x400cfc8c, 0x4ff41350};
        uint32_t epcx[7] = {0x40012345};
        uint8_t epcx_reg_bits = 1;
        #endif
    } ex_info;
};
inline esp_err_t esp_core_dump_image_check() { return dump_result; }
inline esp_err_t esp_core_dump_image_get(size_t* address, size_t* size) {
    *address = dump_address; *size = dump_size; return ESP_OK;
}
inline esp_err_t esp_core_dump_get_summary(esp_core_dump_summary_t* summary) {
    summary->exc_pc = 0x40012345;
    strcpy(summary->exc_task, "task<script>");
    strcpy(reinterpret_cast<char*>(summary->app_elf_sha256), "crashed-elf-hash");
    return ESP_OK;
}
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
inline esp_err_t esp_core_dump_get_panic_reason(char* reason, size_t size) {
    snprintf(reason, size, "panic: \"bad pointer\" <script>"); return ESP_OK;
}
#endif
''',
            "esp_system.h": r'''
#pragma once
enum esp_reset_reason_t { ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC,
    ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT,
    ESP_RST_SDIO
    #if ESP_IDF_VERSION_MAJOR >= 5
    , ESP_RST_USB, ESP_RST_JTAG, ESP_RST_EFUSE, ESP_RST_PWR_GLITCH, ESP_RST_CPU_LOCKUP
    #endif
};
inline esp_reset_reason_t test_reset_reason = ESP_RST_PANIC;
inline esp_reset_reason_t esp_reset_reason() { return test_reset_reason; }
inline const char* esp_get_idf_version() { return "test-sdk"; }
''',
            "esp_app_desc.h": r'''
#pragma once
#if ESP_IDF_VERSION_MAJOR < 5
#error "esp_app_desc.h is unavailable in the legacy SDK"
#endif
inline int esp_app_get_elf_sha256(char* output, size_t size) {
    return snprintf(output, size, "current-elf-hash");
}
''',
            "esp_ota_ops.h": r'''
#pragma once
inline int esp_ota_get_app_elf_sha256(char* output, size_t size) {
    return snprintf(output, size, "current-elf-hash");
}
''',
            "Arduino.h": r'''
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
struct TestSerial {
    bool connected = false;
    std::string output;
    void begin(unsigned long) {}
    explicit operator bool() const { return connected; }
    void print(const char* line) { output += line; }
};
inline TestSerial Serial;
inline unsigned long millis() { return 42; }
''',
            "esp_heap_caps.h": r'''
#pragma once
#include <cassert>
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline unsigned allocations = 0;
inline bool fail_allocation = false;
inline void* heap_caps_malloc(size_t bytes, unsigned caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ++allocations;
    return fail_allocation ? nullptr : malloc(bytes);
}
inline void heap_caps_free(void* memory) { free(memory); }
''',
            "esp_random.h": "#pragma once\ninline unsigned esp_random() { return 123; }\n",
            "config_manager.h": "#pragma once\nstruct DeviceConfig { bool basic_auth_enabled = true; };\n",
            "web_portal_state.h": "#pragma once\n#include \"config_manager.h\"\nDeviceConfig* web_portal_get_current_config();\nbool web_portal_is_ap_mode_active();\n",
            "web_portal_auth.h": "#pragma once\n#include <ESPAsyncWebServer.h>\nbool portal_auth_gate(AsyncWebServerRequest* request);\n",
            "ESPAsyncWebServer.h": r'''
#pragma once
#include <Arduino.h>
#include <map>
#include <memory>
class String : public std::string {
public:
    using std::string::string;
    String() = default;
    bool isEmpty() const { return empty(); }
};
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;
    size_t write(const uint8_t* data, size_t length) {
        for (size_t index = 0; index < length; ++index) write(data[index]);
        return length;
    }
    size_t print(const char* text) { return write(reinterpret_cast<const uint8_t*>(text), strlen(text)); }
};
class AsyncWebServerResponse {
public:
    virtual ~AsyncWebServerResponse() = default;
    void addHeader(const char* name, const char* value) { headers[name] = value; }
    int code() const { return _code; }
    size_t length() const { return _contentLength; }
    virtual bool _sourceValid() const { return true; }
    virtual size_t _fillBuffer(uint8_t*, size_t) { return 0; }
    std::map<std::string, std::string> headers;
    std::string body;
protected:
    int _code = 200;
    String _contentType;
    bool _sendContentLength = false;
    bool _chunked = false;
    size_t _contentLength = 0;
};
class AsyncAbstractResponse : public AsyncWebServerResponse {};
struct TestParam {
    String text;
    const String& value() const { return text; }
};
class AsyncWebServerRequest {
public:
    bool hasParam(const char* name) const { return params.count(name); }
    TestParam* getParam(const char* name) { return &params.at(name); }
    AsyncWebServerResponse* beginResponse(int code, const char*, const char* body) {
        struct Basic : AsyncWebServerResponse {
            Basic(int code, const char* text) { _code = code; body = text; }
        };
        return new Basic(code, body);
    }
    void send(AsyncWebServerResponse* value) { response.reset(value); }
    std::map<std::string, TestParam> params;
    std::unique_ptr<AsyncWebServerResponse> response;
};
''',
            "WebResponseImpl.h": '#pragma once\n#include "ESPAsyncWebServer.h"\n',
            "ChunkPrint.h": r'''
#pragma once
#include "ESPAsyncWebServer.h"
class ChunkPrint : public Print {
public:
    ChunkPrint(uint8_t* output, size_t skip, size_t length) : output_(output), skip_(skip), length_(length) {}
    size_t write(uint8_t byte) override {
        if (position_ >= skip_ && position_ - skip_ < length_) output_[position_ - skip_] = byte;
        ++position_;
        return 1;
    }
private:
    uint8_t* output_;
    size_t skip_, length_, position_ = 0;
};
''',
            "freertos/FreeRTOS.h": "#pragma once\n#define pdTRUE 1\n#define pdPASS 1\n#define pdMS_TO_TICKS(value) (value)\n#define portMAX_DELAY 0xffffffffU\n",
            "freertos/task.h": r'''
#pragma once
inline bool task_failure = false;
inline unsigned tasks_created = 0;
inline void (*scheduled_task)(void*) = nullptr;
inline void* scheduled_argument = nullptr;
inline unsigned last_delay = 0;
inline int xTaskCreate(void (*task)(void*), const char*, unsigned stack, void* argument, unsigned, void*) {
    assert(stack == 4096);
    if (task_failure) return 0;
    ++tasks_created;
    scheduled_task = task;
    scheduled_argument = argument;
    return pdPASS;
}
inline void vTaskDelay(unsigned delay) { last_delay = delay; }
inline void vTaskDelete(void*) {}
''',
            "freertos/semphr.h": r'''
#pragma once
#include <mutex>
struct StaticSemaphore_t { std::mutex mutex; };
using SemaphoreHandle_t = StaticSemaphore_t*;
inline bool refuse_capture = false;
inline void (*after_unlock)() = nullptr;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* storage) { return storage; }
inline int xSemaphoreTake(SemaphoreHandle_t handle, unsigned timeout) {
    if (timeout == portMAX_DELAY) { handle->mutex.lock(); return pdTRUE; }
    return !refuse_capture && handle->mutex.try_lock() ? pdTRUE : 0;
}
inline void xSemaphoreGive(SemaphoreHandle_t handle) {
    handle->mutex.unlock();
    if (after_unlock) {
        auto callback = after_unlock;
        after_unlock = nullptr;
        callback();
    }
}
''',
            "probe.cpp": r'''
#include "log_manager.h"
#include "remote_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <atomic>
#include <cassert>
#include <cstring>
#include <thread>
#include <vector>
#include "esp_partition.h"
#include "freertos/task.h"
#if TEST_ASSERTIONS_DISABLED
#define NDEBUG
#endif
#include "web_portal_logs.cpp"
#if TEST_ASSERTIONS_DISABLED
#undef NDEBUG
#include <cassert>
#endif
#include "esp_system.h"
#if ESP_IDF_VERSION_MAJOR >= 5
#include "esp_app_desc.h"
#else
#include "esp_ota_ops.h"
#endif
#define FIRMWARE_VERSION "test-version"
std::atomic<bool> ota{false};
bool ota_activity_is_active() { return ota.load(); }
#if HAS_REMOTE_LOG
DeviceConfig config;
bool ap_mode = false;
bool authenticated = true;
DeviceConfig* web_portal_get_current_config() { return &config; }
bool web_portal_is_ap_mode_active() { return ap_mode; }
bool portal_auth_gate(AsyncWebServerRequest*) { return !config.basic_auth_enabled || authenticated; }
#endif
int main() {
#if !HAS_REMOTE_LOG
    log_init(115200);
    Serial.connected = true;
    LOGI("SYS", "serial");
    assert(Serial.output == "[42ms] I SYS: serial\n");
    assert(allocations == 0);
    return 0;
#else
#if TEST_FAILURE
    fail_allocation = true;
#endif
    log_init(115200);
    #if !TEST_FAILURE
    remote_log_init();
    #endif
    AsyncWebServerRequest denied;
    ap_mode = true;
    handleGetLogs(&denied);
    assert(denied.response->code() == 403);
    handleGetCrashLog(&denied);
    assert(denied.response->code() == 403);
    handleDownloadCrashLog(&denied);
    assert(denied.response->code() == 403);
    ap_mode = false;
    config.basic_auth_enabled = false;
    authenticated = false;
    {
        AsyncWebServerRequest open;
        assert(portal_logs_full_mode_enabled());
        handleGetLogs(&open);
        assert(open.response && open.response->code() == 200);
    }
    config.basic_auth_enabled = true;
    authenticated = false;
    AsyncWebServerRequest unauthorized;
    handleGetLogs(&unauthorized);
    assert(!unauthorized.response);
    handleGetCrashLog(&unauthorized);
    assert(!unauthorized.response);
    handleDownloadCrashLog(&unauthorized);
    assert(!unauthorized.response);
    authenticated = true;
#if DEBUG_CRASH_API_ENABLED
    AsyncWebServerRequest injection;
    injection.params["mode"].text = "abort";
    injection.params["confirm"].text = "crash";
    ap_mode = true;
    handleDebugCrash(&injection);
    assert(injection.response->code() == 403 && tasks_created == 0);
    ap_mode = false;
    authenticated = false;
    injection.response.reset();
    handleDebugCrash(&injection);
    assert(!injection.response && tasks_created == 0);
    authenticated = true;
    ota = true;
    handleDebugCrash(&injection);
    assert(injection.response->code() == 503 && tasks_created == 0);
    ota = false;
    injection.params.erase("confirm");
    handleDebugCrash(&injection);
    assert(injection.response->code() == 400 && tasks_created == 0);
    injection.params["confirm"].text = "wrong";
    handleDebugCrash(&injection);
    assert(injection.response->code() == 400 && tasks_created == 0);
    injection.params["confirm"].text = "crash";
    injection.params["mode"].text = "wrong";
    handleDebugCrash(&injection);
    assert(injection.response->code() == 400 && tasks_created == 0);
    injection.params["mode"].text = "abort";
    task_failure = true;
    handleDebugCrash(&injection);
    assert(injection.response->code() == 503 && !debug_crash_pending && tasks_created == 0);
    task_failure = false;
    for (const char* name : {"abort", "assert", "invalid_write"}) {
        injection.params["mode"].text = name;
        handleDebugCrash(&injection);
        #if TEST_ASSERTIONS_DISABLED
        if (strcmp(name, "assert") == 0) {
            assert(injection.response->code() == 501 && !debug_crash_pending);
            continue;
        }
        #endif
        assert(injection.response->code() == 202 && debug_crash_pending);
        assert(injection.response->headers["Cache-Control"] == "no-store");
        auto expected_mode = strcmp(name, "abort") == 0 ? DebugCrashMode::Abort :
            (strcmp(name, "assert") == 0 ? DebugCrashMode::Assertion : DebugCrashMode::InvalidWrite);
        assert(reinterpret_cast<uintptr_t>(scheduled_argument) == static_cast<uintptr_t>(expected_mode));
        unsigned count = tasks_created;
        handleDebugCrash(&injection);
        assert(injection.response->code() == 409 && tasks_created == count);
        ota = true;
        scheduled_task(scheduled_argument);
        assert(last_delay == 500 && !debug_crash_pending);
        ota = false;
    }
    assert(tasks_created == (TEST_ASSERTIONS_DISABLED ? 2U : 3U));
#endif
#if REMOTE_LOG_BUFFER_RECORDS == 0 || TEST_FAILURE
    assert(!remote_log_available());
    assert(!remote_log_snapshot(false, false, 0, 32));
    assert(allocations == (REMOTE_LOG_BUFFER_RECORDS ? 1U : 0U));
    Serial.connected = true;
    LOGI("SYS", "serial");
    assert(Serial.output == "[42ms] I SYS: serial\n");
    AsyncWebServerRequest unavailable;
    handleGetLogs(&unavailable);
    assert(unavailable.response->body.find("\"available\":false") != std::string::npos);
    #if TEST_FAILURE && REMOTE_LOG_BUFFER_RECORDS > 0
    fail_allocation = false;
    remote_log_init();
    assert(remote_log_available() && allocations == 2);
    remote_log_init();
    assert(allocations == 2);
    remote_log_append("retry");
    const auto* recovered = remote_log_snapshot(false, false, 0, 1);
    assert(recovered && recovered->count == 1 && strcmp(recovered->records[0].line, "retry") == 0);
    remote_log_release_snapshot();
    #endif
    return 0;
#else
    assert(remote_log_available() && allocations == 1);
    LOGI("SYS", "boot one");
    LOGI("SYS", "boot two");
    LOGI("SYS", "boot overflow");
    assert(Serial.output.empty());
    remote_log_finish_boot();
    const auto* boot = remote_log_snapshot(true, false, 0, 32);
    assert(boot && boot->count == 2 && boot->boot_complete && boot->boot_truncated);
    assert(boot->boot_id == 123 && strstr(boot->records[0].line, "boot one"));
    assert(!remote_log_snapshot(false, false, 0, 32));
    LOGI("SYS", "after boot");
    assert(strstr(boot->records[0].line, "boot one"));
    remote_log_release_snapshot();
    auto* recent = remote_log_snapshot(false, true, 0, 100);
    assert(recent && recent->count == 3 && recent->range.missed == 1);
    assert(recent->records[0].sequence == 2 && recent->range.cursor == 4);
    remote_log_release_snapshot();
    refuse_capture = true;
    Serial.connected = true;
    LOGI("SYS", "contention");
    refuse_capture = false;
    ota = true;
    LOGI("SYS", "ota");
    assert(!remote_log_snapshot(false, false, 0, 32));
    ota = false;
    recent = remote_log_snapshot(false, true, 4, 32);
    assert(recent && recent->count == 0 && recent->dropped == 2);
    remote_log_release_snapshot();
    assert(Serial.output.find("contention") != std::string::npos);
    assert(Serial.output.find("ota") != std::string::npos);
    Serial.output.clear();
    Serial.connected = false;
    LOGE("SYS", "repeat");
    Serial.connected = true;
    LOGE("SYS", "repeat");
    LOGE("SYS", "repeat");
    assert(Serial.output == "[42ms] E SYS: repeat\n");
    std::vector<std::thread> writers;
    for (unsigned worker = 0; worker < 4; ++worker) {
        writers.emplace_back([worker] {
            for (unsigned index = 0; index < 100; ++index) LOGI("Worker", "%u:%u", worker, index);
        });
    }
    for (auto& writer : writers) writer.join();
    recent = remote_log_snapshot(false, false, 0, 32);
    assert(recent && recent->count == 3);
    for (size_t index = 1; index < recent->count; ++index)
        assert(recent->records[index].sequence == recent->records[index - 1].sequence + 1);
    assert(allocations == 1);
    const uint32_t snapshot_newest = recent->range.newest;
    remote_log_release_snapshot();
    after_unlock = [] {
        remote_log_append("overwrite one");
        remote_log_append("overwrite two");
        remote_log_append("overwrite three");
    };
    recent = remote_log_snapshot(false, false, 0, 32);
    assert(recent && recent->range.newest == snapshot_newest && recent->count == 3);
    assert(recent->range.missed == 0 && recent->range.cursor == snapshot_newest);
    assert(recent->records[0].sequence == snapshot_newest - 2);
    assert(recent->records[2].sequence == snapshot_newest);
    remote_log_release_snapshot();
    const std::string escaped_line(REMOTE_LOG_LINE_BYTES - 1, '\x01');
    remote_log_append(escaped_line.c_str());
    remote_log_append("quote=\" newline=\n slash=\\ <script>");
    AsyncWebServerRequest invalid;
    invalid.params["after"].text = "4294967296";
    handleGetLogs(&invalid);
    assert(invalid.response->code() == 400);
    invalid.params["after"].text = "-1";
    handleGetLogs(&invalid);
    assert(invalid.response->code() == 400);
    AsyncWebServerRequest request;
    request.params["boot_id"].text = "999";
    request.params["after"].text = "0";
    handleGetLogs(&request);
    assert(request.response->code() == 200);
    assert(request.response->headers["Cache-Control"] == "no-store");
    AsyncWebServerRequest busy;
    handleGetLogs(&busy);
    assert(busy.response->code() == 429);
    std::string json;
    uint8_t chunk[7];
    assert(request.response->_fillBuffer(chunk, 0) == 0);
    while (const size_t count = request.response->_fillBuffer(chunk, sizeof(chunk)))
        json.append(reinterpret_cast<char*>(chunk), count);
    assert(json.size() == request.response->length());
    JsonDocument document;
    assert(!deserializeJson(document, json));
    assert(document["reset"] == true);
    assert(document["records"].size() == 3);
    assert(document["records"][1]["line"].as<std::string>() == escaped_line);
    const char* line = document["records"][2]["line"];
    assert(!strcmp(line, "quote=\" newline=\n slash=\\ <script>"));
    ota = true;
    assert(!request.response->_sourceValid());
    ota = false;
    request.response.reset();
    assert(remote_log_snapshot(false, false, 0, 32));
    remote_log_release_snapshot();
    Serial.output.clear();
    {
        TEST_STARTUP_DIAGNOSTICS
    }
    assert(Serial.output.find("Reset reason: Panic (4)") != std::string::npos);
    assert(Serial.output.find("SDK: test-sdk") != std::string::npos);
    assert(Serial.output.find("Firmware ELF SHA256: current-elf-hash") != std::string::npos);
    AsyncWebServerRequest crash;
    handleGetCrashLog(&crash);
    #if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    assert(crash.response->code() == 200);
    assert(!deserializeJson(document, crash.response->body));
    assert(document["available"] == true);
    assert(document["size"] == 2051);
    assert(document["task"] == "task<script>");
    assert(document["pc"] == "0x40012345");
    assert(document["elf_sha256"] == "crashed-elf-hash");
    #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
    assert(document["panic_reason"] == "panic: \"bad pointer\" <script>");
    #else
    assert(document["panic_reason"].isNull());
    #endif
    assert(document["current_reset_reason"] == 4);
    assert(document["current_elf_sha256"] == "current-elf-hash");
    #if CONFIG_IDF_TARGET_ARCH_RISCV
    assert(document["architecture"] == "riscv");
    assert(document["exception_cause"] == 7);
    assert(document["trap_value"] == "0x500d2000");
    assert(document["registers"]["RA"] == "0x400cfc8c");
    assert(document["registers"]["SP"] == "0x4ff41350");
    assert(document["registers"]["MCAUSE"] == "0x00000007");
    assert(document["registers"]["A7"] == "0x00000000");
    #else
    assert(document["architecture"] == "xtensa");
    assert(document["exception_cause"] == 29);
    assert(document["fault_address"] == "0x500d2000");
    assert(document["registers"]["A1"] == "0x4ff41350");
    assert(document["registers"]["EPC1"] == "0x40012345");
    assert(document["registers"]["EPC2"].isNull());
    assert(document["registers"]["A15"] == "0x00000000");
    #endif
    assert(remote_log_snapshot(false, false, 0, 1));
    remote_log_release_snapshot();
    dump_result = ESP_ERR_NOT_FOUND;
    handleGetCrashLog(&crash);
    assert(crash.response->body.find("not_found") != std::string::npos);
    handleDownloadCrashLog(&crash);
    assert(crash.response->code() == 404);
    dump_result = ESP_ERR_INVALID_CRC;
    handleGetCrashLog(&crash);
    assert(crash.response->body.find("invalid_dump") != std::string::npos);
    handleDownloadCrashLog(&crash);
    assert(crash.response->code() == 409);
    dump_result = ESP_OK;
    partition_present = false;
    handleGetCrashLog(&crash);
    assert(crash.response->body.find("no_partition") != std::string::npos);
    partition_present = true;
    dump_address = 4095;
    handleDownloadCrashLog(&crash);
    assert(crash.response->code() == 409);
    dump_address = 4096;
    dump_size = 4097;
    handleDownloadCrashLog(&crash);
    assert(crash.response->code() == 409);
    dump_size = 2051;
    ota = true;
    handleGetCrashLog(&crash);
    assert(crash.response->code() == 503);
    handleDownloadCrashLog(&crash);
    assert(crash.response->code() == 503);
    ota = false;
    AsyncWebServerRequest download;
    handleDownloadCrashLog(&download);
    assert(download.response->code() == 200 && download.response->length() == dump_size);
    assert(download.response->headers["Cache-Control"] == "no-store");
    assert(download.response->headers["Content-Disposition"].find("device-coredump.bin") != std::string::npos);
    handleDownloadCrashLog(&crash);
    assert(crash.response->code() == 429);
    std::vector<uint8_t> binary;
    uint8_t buffer[2048];
    while (size_t count = download.response->_fillBuffer(buffer, sizeof(buffer)))
        binary.insert(binary.end(), buffer, buffer + count);
    assert(binary.size() == dump_size && max_read == 1024);
    for (size_t index = 0; index < binary.size(); ++index) assert(binary[index] == index % 256);
    ota = true;
    assert(!download.response->_sourceValid());
    ota = false;
    download.response.reset();
    handleDownloadCrashLog(&download);
    assert(download.response->code() == 200);
    read_failure = true;
    assert(download.response->_fillBuffer(buffer, sizeof(buffer)) == 0);
    assert(!download.response->_sourceValid());
    download.response.reset();
    read_failure = false;
    handleDownloadCrashLog(&download);
    assert(download.response->code() == 200);
    #else
    assert(crash.response->body.find("disabled") != std::string::npos);
    handleDownloadCrashLog(&crash);
    assert(crash.response->code() == 404);
    #endif
#endif
#endif
}
''',
        }
        startup = (ROOT / "src/app/app.ino").read_text().split('LOGI("SYS", "Boot");', 1)[1].split('LOGI("SYS", "Chip:', 1)[0]
        files["probe.cpp"] = files["probe.cpp"].replace("TEST_STARTUP_DIAGNOSTICS", startup)
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            for name, text in files.items():
                path = work / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            for name in ("remote_log.h", "remote_log.cpp", "log_manager.h", "log_manager.cpp", "ota_activity.h", "web_portal_logs.h", "web_portal_logs.cpp"):
                (work / name).write_bytes((ROOT / "src/app" / name).read_bytes())
            for capacity, failure, coredump, riscv, assertions_disabled in (
                    (0, 0, 1, 1, 0), (3, 0, 0, 1, 0), (3, 0, 1, 1, 0),
                    (3, 0, 1, 0, 0), (3, 1, 1, 1, 0), (3, 0, 1, 1, 1)):
                with self.subTest(capacity=capacity, failure=failure, coredump=coredump, riscv=riscv,
                                  assertions_disabled=assertions_disabled):
                    binary = work / "probe"
                    atomic_flags = [] if riscv else ["-U__GCC_ATOMIC_INT_LOCK_FREE", "-D__GCC_ATOMIC_INT_LOCK_FREE=1"]
                    subprocess.run([
                        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
                        *atomic_flags,
                        "-DESP32", "-DARDUINO_USB_CDC_ON_BOOT=1",
                        f"-DHAS_REMOTE_LOG={int(capacity > 0)}",
                        f"-DREMOTE_LOG_BUFFER_RECORDS={capacity or 3}", f"-DTEST_FAILURE={failure}",
                        f"-DCONFIG_ESP_COREDUMP_ENABLE_TO_FLASH={coredump}", f"-DCONFIG_ESP_COREDUMP_DATA_FORMAT_ELF={coredump}",
                        f"-DCONFIG_IDF_TARGET_ARCH_RISCV={riscv}", f"-DCONFIG_IDF_TARGET_ARCH_XTENSA={1 - riscv}",
                        f"-DCONFIG_IDF_TARGET_ESP32S3={1 - riscv}",
                        f"-DESP_IDF_VERSION_MAJOR={5 if riscv else 4}",
                        f"-DDEBUG_CRASH_API_ENABLED={riscv}",
                        f"-DTEST_ASSERTIONS_DISABLED={assertions_disabled}",
                        "-I", directory, "-I", str(pathlib.Path.home() / "Arduino/libraries/ArduinoJson/src"),
                        str(work / "probe.cpp"), str(work / "remote_log.cpp"),
                        str(work / "log_manager.cpp"), "-o", str(binary),
                    ], check=True)
                    subprocess.run([str(binary)], check=True)
                    symbols = subprocess.check_output(["nm", "-C", str(binary)], text=True)
                    self.assertEqual("handleDebugCrash(AsyncWebServerRequest*)" in symbols, bool(riscv and capacity))
                    if not capacity:
                        self.assertNotIn("remote_log_", symbols)
                        self.assertNotIn("handleGetLogs", symbols)
                        self.assertNotIn("handleGetCrashLog", symbols)
                        self.assertNotIn("handleDownloadCrashLog", symbols)


if __name__ == "__main__":
    unittest.main()