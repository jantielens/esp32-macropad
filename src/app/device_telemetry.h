#ifndef DEVICE_TELEMETRY_H
#define DEVICE_TELEMETRY_H

#include <ArduinoJson.h>
#include "fs_health.h"
#include "power_config.h"

struct DeviceMemorySnapshot {
	size_t heap_free_bytes;
	size_t heap_min_free_bytes;
	size_t heap_largest_free_block_bytes;
	size_t heap_internal_free_bytes;
	size_t heap_internal_min_free_bytes;
	size_t psram_free_bytes;
	size_t psram_min_free_bytes;
	size_t psram_largest_free_block_bytes;
};

// Subset of /api/health "*_min_window" / "*_max_window" band fields needed for sparklines.
// All values are bytes.
struct DeviceHealthWindowBands {
	uint32_t heap_internal_free_min_window;
	uint32_t heap_internal_free_max_window;

	uint32_t psram_free_min_window;
	uint32_t psram_free_max_window;
};

inline void device_telemetry_append_fs_health(JsonDocument &doc, const FSHealthStats &fs) {
	const char* backend = fs.backend == FS_BACKEND_SDMMC ? "sdmmc" : "littlefs";
	doc["fs_backend"] = backend;
	doc["fs_mounted"] = fs.storage_mounted;
	if (fs.storage_mounted && fs.storage_total_bytes > 0) {
		doc["fs_used_bytes"] = fs.storage_used_bytes;
		doc["fs_total_bytes"] = fs.storage_total_bytes;
	} else {
		doc["fs_used_bytes"] = nullptr;
		doc["fs_total_bytes"] = nullptr;
	}

	if (fs.backend != FS_BACKEND_SDMMC) {
		doc["fs_card_type"] = nullptr;
		return;
	}

	switch (fs.card_type) {
		case FS_CARD_TYPE_NONE: doc["fs_card_type"] = "none"; break;
		case FS_CARD_TYPE_SD: doc["fs_card_type"] = "sd"; break;
		case FS_CARD_TYPE_SDHC: doc["fs_card_type"] = "sdhc"; break;
		default: doc["fs_card_type"] = "unknown"; break;
	}
}

// Initializes cached values used by device telemetry (safe to call multiple times).
// This exists to avoid re-entrant calls into ESP-IDF image helpers from different tasks.
void device_telemetry_init();

// Cache the current WiFi RSSI value.  Call once after WiFi connects (and
// optionally after a reconnect).  All subsequent health publishes use the
// cached value instead of issuing a live WiFi.RSSI() RPC.
void device_telemetry_cache_rssi();

// Return the most recently cached RSSI.  Sets *valid = false if cache is empty.
int16_t device_telemetry_get_cached_rssi(bool *valid = nullptr);

// Cached flash/sketch metadata helpers.
size_t device_telemetry_sketch_size();
size_t device_telemetry_free_sketch_space();

// Fill a JsonDocument with device telemetry for the web API (/api/health).
void device_telemetry_fill_api(JsonDocument &doc);

// Fill a JsonDocument with device telemetry optimized for MQTT publishing.
// Intentionally excludes volatile/low-value fields like IP address.
void device_telemetry_fill_mqtt(JsonDocument &doc);

// Fill MQTT telemetry with scoped content (sensors-only, diagnostics-only, or all).
void device_telemetry_fill_mqtt_scoped(JsonDocument &doc, MqttPublishScope scope);

// Get current CPU usage percentage (0-100).
// Returns -1 when runtime stats are unavailable (treated as unknown).
int device_telemetry_get_cpu_usage();

// Get current CPU usage percentage for one FreeRTOS core (0-100).
// Returns -1 when the core is unavailable or runtime stats are unavailable.
int device_telemetry_get_cpu_usage_for_core(uint8_t core);

// Copy the aggregate and up to max_cores per-core CPU usage values from one
// coherent telemetry sample. Values are -1 when runtime stats are unavailable.
void device_telemetry_get_cpu_usage_snapshot(int* aggregate,
											 int* per_core_values,
											 uint8_t max_cores);

// Initialize CPU monitoring background task.
// Must be called once during setup.
void device_telemetry_start_cpu_monitoring();

// Start 200ms health-window sampling (min/max fields between /api/health polls).
// Must be called once during setup.
void device_telemetry_start_health_window_sampling();

// Capture a point-in-time memory snapshot (heap/internal heap/PSRAM).
DeviceMemorySnapshot device_telemetry_get_memory_snapshot();

// Capture a merged snapshot of the current health-window band values.
// Returns false if bands are unavailable (early boot), in which case callers should
// fall back to instantaneous values.
bool device_telemetry_get_health_window_bands(DeviceHealthWindowBands* out_bands);

// Convenience logging helper (single line) using logger.
void device_telemetry_log_memory_snapshot(const char *tag);

#endif // DEVICE_TELEMETRY_H
