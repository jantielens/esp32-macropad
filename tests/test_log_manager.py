#!/usr/bin/env python3
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class LogManagerTests(unittest.TestCase):
    def test_production_logger(self):
        shim = """
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
inline unsigned long clock_ms = 0;
inline unsigned long millis() { return clock_ms; }
struct SerialStub {
    std::string output;
    void begin(unsigned long) {}
    void print(const char* text) { output += text; }
    explicit operator bool() const { return true; }
};
inline SerialStub Serial;
"""
        probe = r'''
#define BOARD_CONFIG_H
#include "src/app/log_manager.cpp"
#include <cassert>
#include <limits>
int main() {
#ifdef TEST_DEFAULT_LEVEL
    assert(LOG_LEVEL == LOG_LEVEL_DEBUG);
#endif
#ifdef TEST_DEFAULT_DIAGNOSTICS
    assert(!strcmp(LOG_DIAGNOSTICS, "*"));
#endif
    log_init(115200);
    int evaluated = 0;
    LOGD("Probe", "%d", ++evaluated);
    LOGT("Other", "%d", ++evaluated);
    const bool all = LOG_DIAGNOSTICS[0] == '*';
    const bool other = all || strstr(LOG_DIAGNOSTICS, "Other");
    assert(evaluated == (LOG_LEVEL >= LOG_LEVEL_DEBUG ? (other ? 2 : 1) : 0));
    assert(log_diagnostics_enabled("Probe") == (LOG_DIAGNOSTICS[0] != 0));
    assert(log_diagnostics_enabled("Other") == other);
    assert(log_diagnostics_enabled("ProbeExtra") == all);
    Serial.output.clear();
    log_write(LOG_LEVEL_DEBUG, "Probe", "direct debug");
    assert(Serial.output.empty() == (LOG_LEVEL < LOG_LEVEL_DEBUG));
    Serial.output.clear();
    log_duration("Probe", "duration", 0);
    assert(Serial.output.empty() == (LOG_LEVEL < LOG_LEVEL_INFO));
    Serial.output.clear();
    log_write(static_cast<LogLevel>(0), "Probe", "invalid");
    log_write(static_cast<LogLevel>(5), "Probe", "invalid");
    assert(Serial.output.empty());
    if (LOG_LEVEL == 0) {
        LOGE("Probe", "%d", ++evaluated);
        assert(evaluated == 0 && Serial.output.empty());
        return 0;
    }
    LOGE("tag\n", "control\x1b");
    assert(Serial.output.find("E tag : control \n") != std::string::npos);
    Serial.output.clear();
    LOGE("Probe", "first\nsecond\rthird");
    assert(Serial.output.find("first second third\n") != std::string::npos);
    Serial.output.clear();
    std::string long_text(500, 'x');
    LOGE("Probe", "%s", long_text.c_str());
    assert(Serial.output.find("[truncated]") != std::string::npos);
    Serial.output.clear();
    for (int index = 0; index < 5; ++index) LOGE("Retry", "failure");
    assert(Serial.output == "[0ms] E Retry: failure\n");
    clock_ms = 5000;
    LOGE("Retry", "failure");
    assert(Serial.output.find("[suppressed=4]") != std::string::npos);
    Serial.output.clear();
    LOGE("Slots", "slot=%d failure", 1);
    LOGE("Slots", "slot=%d failure", 2);
    assert(Serial.output.find("slot=1") != std::string::npos);
    assert(Serial.output.find("slot=2") != std::string::npos);
    Serial.output.clear();
    clock_ms = std::numeric_limits<unsigned long>::max() - 100;
    LOGE("Probe", "wraparound failure");
    clock_ms = 100;
    LOGE("Probe", "wraparound failure");
    clock_ms = 5000;
    LOGE("Probe", "wraparound failure");
    assert(Serial.output.find("[suppressed=1]") != std::string::npos);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            (work / "Arduino.h").write_text(shim)
            (work / "probe.cpp").write_text(probe)
            for level in (None, "0", "1", "2", "LOG_LEVEL_INFO", "LOG_LEVEL_DEBUG"):
                for diagnostics in (None, "", "Probe", "Other,Probe", "*"):
                    with self.subTest(level=level, diagnostics=diagnostics):
                        binary = work / "probe"
                        subprocess.run([
                            "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            f"-DLOG_LEVEL={level}" if level is not None else "-DTEST_DEFAULT_LEVEL",
                            f'-DLOG_DIAGNOSTICS="{diagnostics}"' if diagnostics is not None else "-DTEST_DEFAULT_DIAGNOSTICS",
                            "-I", str(work), "-I", str(ROOT),
                            str(work / "probe.cpp"), "-o", str(binary),
                        ], check=True)
                        subprocess.run([str(binary)], check=True)

    def test_memory_log_sample_validity(self):
        source = (ROOT / "src/app/device_telemetry.cpp").read_text()

        def function(signature):
            start = source.index(signature)
            return source[start:source.index("\n}", start) + 2]

        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
uint32_t clock_ms = 0;
unsigned lock_depth = 0, pool_walks = 0;
uint32_t millis() { return clock_ms; }
#define portENTER_CRITICAL(mux) (++lock_depth)
#define portEXIT_CRITICAL(mux) (--lock_depth)
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_DMA 4
constexpr uint32_t kInternalPoolWalkPeriodMs = 1000;
size_t g_cached_internal_largest = 0, g_cached_internal_free = 0;
size_t g_cached_dma_internal_largest = 0;
bool g_cached_internal_largest_valid = false;
uint32_t g_last_internal_pool_walk_ms = 0;
size_t live_free = 270856, live_largest = 0, psram_largest = 0;
size_t sample_free = 334800, sample_largest = 303092;
size_t heap_caps_get_largest_free_block(unsigned caps) {
    assert(lock_depth == 0);
    ++pool_walks;
    return caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) ? sample_largest : 128;
}
size_t heap_caps_get_free_size(unsigned) { assert(lock_depth == 0); return sample_free; }
size_t test_strlcpy(char* target, const char* text, size_t capacity) {
    std::snprintf(target, capacity, "%s", text);
    return std::strlen(text);
}
#define strlcpy test_strlcpy
char output[512];
void capture_log(const char* format, ...) {
    assert(lock_depth == 0);
    va_list args;
    va_start(args, format);
    std::vsnprintf(output, sizeof(output), format, args);
    va_end(args);
    assert(std::strlen(output) < 192);
}
#define LOGI(module, ...) capture_log(__VA_ARGS__)
void get_memory_snapshot(size_t* heap_free, size_t* heap_min, size_t* heap_largest,
                         size_t* internal_free, size_t* internal_min, size_t* psram_free,
                         size_t* psram_min, size_t* out_psram_largest) {
    *heap_free = *heap_min = *internal_free = *internal_min = live_free;
    *heap_largest = live_largest;
    *psram_free = *psram_min = 30991104;
    *out_psram_largest = psram_largest;
}
'''
        harness += function("static int compute_fragmentation_percent(")
        harness += function("static void sample_internal_largest_if_due(")
        harness += function("void device_telemetry_log_memory_snapshot(")
        harness += r'''
int main() {
    device_telemetry_log_memory_snapshot("boot");
    assert(std::strstr(output, "frag=100") == nullptr);
    assert(std::strstr(output, "pl=na"));
#if TELEMETRY_CACHE_INTERNAL_POOL_WALK
    assert(std::strstr(output, "hl_sample=na hf_sample=na frag_sample=na pool_age_ms=na"));
    clock_ms = 100;
    sample_internal_largest_if_due(clock_ms);
    assert(pool_walks == 2);
    clock_ms = 150;
    device_telemetry_log_memory_snapshot("setup");
    assert(std::strstr(output, "hf=270856"));
    assert(std::strstr(output, "hl_sample=303092 hf_sample=334800 frag_sample=9 pool_age_ms=50"));
    assert(pool_walks == 2);
    clock_ms = 1200;
    device_telemetry_log_memory_snapshot("stale");
    assert(std::strstr(output, "frag_sample=na pool_age_ms=1100"));
    sample_internal_largest_if_due(clock_ms);
    device_telemetry_log_memory_snapshot("fresh");
    assert(std::strstr(output, "frag_sample=9 pool_age_ms=0"));
    sample_largest = sample_free + 1;
    clock_ms = 2300;
    sample_internal_largest_if_due(clock_ms);
    device_telemetry_log_memory_snapshot("inconsistent");
    assert(std::strstr(output, "frag_sample=na"));
    sample_largest = 0;
    clock_ms = 3400;
    sample_internal_largest_if_due(clock_ms);
    device_telemetry_log_memory_snapshot("unavailable");
    assert(std::strstr(output, "frag_sample=na"));
    g_last_internal_pool_walk_ms = UINT32_MAX - 49;
    g_cached_internal_largest = 50;
    g_cached_internal_free = 100;
    clock_ms = 100;
    device_telemetry_log_memory_snapshot("wraparound");
    assert(std::strstr(output, "frag_sample=50 pool_age_ms=150"));
#else
    sample_internal_largest_if_due(100);
    assert(pool_walks == 0);
    assert(std::strstr(output, "hl=na frag=na"));
    live_free = 100;
    live_largest = 150;
    device_telemetry_log_memory_snapshot("inconsistent");
    assert(std::strstr(output, "hl=na frag=na"));
    live_largest = 50;
    device_telemetry_log_memory_snapshot("valid");
    assert(std::strstr(output, "hl=50 frag=50"));
    live_free = live_largest = 0;
    device_telemetry_log_memory_snapshot("empty");
    assert(std::strstr(output, "hl=na frag=na"));
#endif
    psram_largest = 1234;
    device_telemetry_log_memory_snapshot("psram");
    assert(std::strstr(output, "pl=1234"));
    psram_largest = 30991105;
    device_telemetry_log_memory_snapshot("psram-inconsistent");
    assert(std::strstr(output, "pl=na"));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            (work / "probe.cpp").write_text(harness)
            for cached in (0, 1):
                with self.subTest(cached=cached):
                    binary = work / "probe"
                    subprocess.run([
                        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-DTELEMETRY_CACHE_INTERNAL_POOL_WALK={cached}",
                        str(work / "probe.cpp"), "-o", str(binary),
                    ], check=True)
                    subprocess.run([str(binary)], check=True)

    def test_wifi_status_and_startup_levels(self):
        startup = {
            "src/app/app.ino": ("Applying loaded brightness",),
            "src/app/config_manager.cpp": ("NVS init start", "NVS init OK", "Loaded brightness", "Display rotation offset"),
            "src/app/drivers/gt911_touch_driver.cpp": ("Initializing touch on", "Hardware reset", "Reset complete"),
            "src/app/device_telemetry.cpp": ("Cached WiFi RSSI",),
        }
        for path, messages in startup.items():
            source = (ROOT / path).read_text()
            for message in messages:
                with self.subTest(path=path, message=message):
                    self.assertRegex(source, r'LOGT\("[^"\n]+", "' + re.escape(message))
        wifi_source = (ROOT / "src/app/wifi_manager.cpp").read_text()
        reasons = re.findall(r"const char\* reason =\s*(.*?);", wifi_source, re.S)
        self.assertEqual(len(reasons), 2)
        harness = r'''
#include <cassert>
#include <cstring>
enum wl_status_t { WL_IDLE_STATUS, WL_NO_SSID_AVAIL, WL_SCAN_COMPLETED, WL_CONNECTED,
                   WL_CONNECT_FAILED, WL_CONNECTION_LOST, WL_DISCONNECTED };
'''
        for index, reason in enumerate(reasons):
            harness += f"const char* status_name_{index}(wl_status_t status) {{ return {reason}; }}\n"
        harness += "int main() {\n"
        for index in range(len(reasons)):
            harness += f'assert(!std::strcmp(status_name_{index}(WL_IDLE_STATUS), "Idle/connecting"));\n'
            harness += f'assert(!std::strcmp(status_name_{index}(WL_DISCONNECTED), "Disconnected"));\n'
            harness += f'assert(std::strstr(status_name_{index}(WL_CONNECT_FAILED), "Connect failed"));\n'
        harness += "}\n"
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            (work / "probe.cpp").write_text(harness)
            binary = work / "probe"
            subprocess.run([
                "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                str(work / "probe.cpp"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)

    def test_retry_deadlines_and_extension_context(self):
        camera_source = (ROOT / "src/app/camera_feed.cpp").read_text()
        camera_start = camera_source.index("void camera_feed_loop()")
        camera_end = camera_source.index("    if (!live_feed_due) return;", camera_start)
        camera_loop = camera_source[camera_start:camera_end] + "}\n"
        mqtt_source = (ROOT / "src/app/mqtt_manager.cpp").read_text()
        mqtt_start = mqtt_source.index("void MqttManager::publishHealthIfDue()")
        mqtt_end = mqtt_source.index("bool MqttManager::attemptConnectWithLWT", mqtt_start)
        extension_source = (ROOT / "src/app/native_extension.cpp").read_text()
        context_start = extension_source.index("struct ExtensionLogContext")
        context_end = extension_source.index("CanvasBuffer* find_canvas_buffer", context_start)
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#define LOGW(...) ((void)0)
#define LOGE(...) ((void)0)
#define LOGI(...) ((void)0)
#define LOGT(...) ((void)0)
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
static uint32_t clock_ms = 0;
static uint32_t millis() { return clock_ms; }
static int64_t esp_timer_get_time() { return int64_t(clock_ms) * 1000; }
static bool ota_activity_is_active() { return false; }
static uint16_t s_rgb565_demand = 0, s_jpeg_demand = 0;
static int s_mux = 0, s_raw_frame = 0;
static uint32_t s_last_capture_ms = 0, capture_attempts = 0;
static bool prepare_ok = false;
struct FeedSlot { bool writing = false; };
static FeedSlot s_slots[1];
struct CameraCaptureSettings { uint32_t feed_target_fps = 5; };
struct CameraMotionSettings { bool enabled = true; uint32_t analyze_every_nth_frame = 1; };
static bool camera_motion_is_enabled() { return true; }
static void camera_feed_release_idle_resources() {}
static void camera_motion_loop() {}
static bool camera_is_detected() { return true; }
static CameraCaptureSettings camera_get_capture_settings() { return {}; }
static CameraMotionSettings camera_motion_get_settings() { return {}; }
static bool camera_feed_ensure_cache() { return true; }
static int8_t camera_feed_claim_writable_slot() { return 0; }
static bool camera_prepare_raw_capture() { ++capture_attempts; return prepare_ok; }
static bool camera_capture_raw_reuse(int*) { return true; }
static void camera_motion_on_raw_frame(int, bool) {}
#define MQTT_MAX_PACKET_SIZE 128
static bool json_overflow = true, publish_ok = false;
static unsigned json_size = 8, serialization_attempts = 0, publish_attempts = 0;
struct DeviceConfig { unsigned mqtt_publish_interval_seconds = 30; };
struct MqttPublishScope {};
template<unsigned Capacity> struct StaticJsonDocument {
    bool overflowed() const { return json_overflow; }
};
static MqttPublishScope power_config_parse_mqtt_publish_scope(DeviceConfig*) { return {}; }
static void device_telemetry_fill_mqtt_scoped(StaticJsonDocument<768>&, MqttPublishScope) { ++serialization_attempts; }
static size_t serializeJson(StaticJsonDocument<768>&, char*, size_t) { return json_size; }
struct Client {
    bool connected() { return true; }
    bool publish(const char*, const uint8_t*, unsigned, bool) { ++publish_attempts; return publish_ok; }
};
struct MqttManager {
    Client _client;
    DeviceConfig config;
    DeviceConfig* _config = &config;
    const char* _health_state_topic = "health";
    unsigned long _last_health_publish_ms = 0, _last_health_attempt_ms = 0;
    bool _health_attempted = false;
    uint32_t _health_publish_failures = 0;
    bool publishEnabled() { return true; }
    void publishHealthIfDue();
};
#define NATIVE_EXTENSION_SLOT_COUNT 2
using TaskHandle_t = void*;
struct LoadedSlot { struct { char id[32]; } info; };
static LoadedSlot s_slots_for_context[2];
static int s_worker_lock = 0;
static TaskHandle_t current_task = nullptr;
static TaskHandle_t xTaskGetCurrentTaskHandle() { return current_task; }
static size_t test_strlcpy(char* target, const char* source, size_t capacity) {
    std::snprintf(target, capacity, "%s", source);
    return std::strlen(source);
}
#define strlcpy test_strlcpy
'''
        context_code = extension_source[context_start:context_end].replace(
            "sizeof(s_slots[0].info.id)", "sizeof(s_slots_for_context[0].info.id)")
        harness += camera_loop + mqtt_source[mqtt_start:mqtt_end] + context_code
        harness += r'''
int main() {
    clock_ms = 200;
    camera_feed_loop();
    assert(capture_attempts == 1 && s_last_capture_ms == 200);
    for (clock_ms = 201; clock_ms < 400; ++clock_ms) camera_feed_loop();
    assert(capture_attempts == 1);
    camera_feed_loop();
    assert(capture_attempts == 2 && s_last_capture_ms == 400);
    prepare_ok = true;
    clock_ms = 600;
    camera_feed_loop();
    assert(capture_attempts == 3);
    MqttManager mqtt;
    clock_ms = 100;
    mqtt.publishHealthIfDue();
    for (clock_ms = 101; clock_ms < 5100; ++clock_ms) mqtt.publishHealthIfDue();
    assert(serialization_attempts == 1);
    mqtt.publishHealthIfDue();
    assert(serialization_attempts == 2);
    clock_ms = 10100;
    json_overflow = false;
    json_size = MQTT_MAX_PACKET_SIZE;
    mqtt.publishHealthIfDue();
    assert(publish_attempts == 0);
    clock_ms = 15100;
    json_size = 8;
    mqtt.publishHealthIfDue();
    assert(publish_attempts == 1 && mqtt._health_publish_failures == 4);
    clock_ms = 20100;
    publish_ok = true;
    mqtt.publishHealthIfDue();
    assert(publish_attempts == 2 && mqtt._health_publish_failures == 0);
    clock_ms = 50099;
    mqtt.publishHealthIfDue();
    assert(publish_attempts == 2);
    clock_ms = 50100;
    mqtt.publishHealthIfDue();
    assert(publish_attempts == 3);
    int task_one = 0, task_two = 0;
    current_task = &task_one;
    {
        ExtensionLogScope first("first", 11);
        {
            ExtensionLogScope nested("nested", 12);
            bool found = false;
            for (const auto& context : s_log_contexts)
                if (context.task == &task_one) { found = true; assert(context.instance_id == 12); }
            assert(found);
        }
        current_task = &task_two;
        {
            ExtensionLogScope second("second", 22);
            for (const auto& context : s_log_contexts) {
                if (context.task == &task_one) assert(context.instance_id == 11 && !strcmp(context.id, "first"));
                if (context.task == &task_two) assert(context.instance_id == 22 && !strcmp(context.id, "second"));
            }
        }
        current_task = &task_one;
    }
    for (const auto& context : s_log_contexts) assert(!context.task);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            work = pathlib.Path(directory)
            (work / "probe.cpp").write_text(harness)
            binary = work / "probe"
            subprocess.run([
                "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-variable", str(work / "probe.cpp"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()