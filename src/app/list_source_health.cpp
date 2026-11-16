#include "list_source_health.h"

#if HAS_DISPLAY && HAS_PSRAM

#include "audio.h"
#include "device_telemetry.h"
#include "display_manager.h"
#include "list_source.h"
#include "version.h"
#include "wifi_manager.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

namespace {

uint32_t health_revision() { return static_cast<uint32_t>(millis()) / 2000; }

const char* cpu_severity(int value) {
    return value < 0 ? "unknown" : value >= 85 ? "critical" : value >= 60 ? "warning" : "normal";
}

ListSourceLoadStep health_step(void**, ListSourceSnapshot* snapshot, const char** error) {
    const DeviceMemorySnapshot memory = device_telemetry_get_memory_snapshot();
    WifiNetworkSnapshot network{};
    wifi_manager_get_network_snapshot(&network);
    const int cpu = device_telemetry_get_cpu_usage();
    bool rssi_valid = false;
    const int16_t rssi = device_telemetry_get_cached_rssi(&rssi_valid);
    const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    BoundedPsramJsonAllocator allocator(8 * 1024);
    JsonDocument item(&allocator);
    auto append = [&](const char* id, const char* title, auto value, const char* unit,
                      const char* severity = nullptr, bool available = true) {
        item.clear();
        item["id"] = id;
        item["title"] = title;
        if (available) item["value"] = value;
        item["unit"] = unit;
        if (severity) item["severity"] = severity;
        if (item.overflowed()) {
            *error = "health metadata allocation failed";
            return false;
        }
        *error = snapshot->append(item.as<JsonObjectConst>());
        return !*error;
    };

    const size_t heap = memory.heap_internal_free_bytes;
    const char* heap_severity = heap > 200 * 1024 ? "normal" : heap > 100 * 1024 ? "warning" : "critical";
    const unsigned psram_percent = psram_total ? memory.psram_free_bytes * uint64_t{100} / psram_total : 0;
    const char* psram_severity = !psram_total ? "unknown" :
        psram_percent >= 35 ? "normal" : psram_percent >= 15 ? "warning" : "critical";
    const bool connected = network.state == WifiNetworkState::Connected;
    const char* wifi_severity = !connected || !rssi_valid ? "unknown" :
        rssi >= -67 ? "normal" : rssi >= -75 ? "warning" : "critical";
    if (!append("cpu", "CPU", cpu, "%", cpu_severity(cpu), cpu >= 0) ||
        !append("heap_internal", "Heap", heap, "B", heap_severity) ||
        !append("psram_free", "PSRAM", memory.psram_free_bytes, "B", psram_severity, psram_total != 0) ||
        !append("wifi_rssi", "WiFi signal", rssi, "dBm", wifi_severity, connected && rssi_valid) ||
        !append("uptime", "Uptime", static_cast<uint32_t>(millis()) / 1000, "s") ||
        !append("wifi_ip", "IP", network.ip, "", nullptr,
                (connected || network.state == WifiNetworkState::Ap) && network.ip[0]) ||
        !append("brightness", "Backlight", display_manager_get_backlight_brightness(), "%"))
        return ListSourceLoadStep::Failed;
#if HAS_AUDIO
    if (!append("volume", "Volume", audio_get_volume(), "%")) return ListSourceLoadStep::Failed;
#endif
    if (!append("chip", "Chip", ESP.getChipModel(), "") ||
        !append("firmware", "Firmware", FIRMWARE_VERSION, ""))
        return ListSourceLoadStep::Failed;
    return ListSourceLoadStep::Complete;
}

void health_finish(void*) {}

const ListSourceField fields[] = {
    {"id", "Stable telemetry key", ListSourceFieldType::String, false},
    {"title", "Metric name", ListSourceFieldType::String, false},
    {"value", "Raw number or text; absent when unavailable", ListSourceFieldType::Scalar, true},
    {"unit", "Unit of the raw value, or empty for text", ListSourceFieldType::String, false},
    {"severity", "normal, warning, critical, or unknown; absent for informational rows",
        ListSourceFieldType::String, true},
};
const ListSourceDescriptor source{"[health:metrics]", "Device health", fields,
    sizeof(fields) / sizeof(fields[0]), health_revision, health_step, health_finish};
}

bool list_source_health_init() { return list_source_register(&source); }

#endif
