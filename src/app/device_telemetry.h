#ifndef DEVICE_TELEMETRY_H
#define DEVICE_TELEMETRY_H

#include <ArduinoJson.h>
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

	uint32_t heap_internal_largest_min_window;
	uint32_t heap_internal_largest_max_window;
};

// Initializes cached values used by device telemetry (safe to call multiple times).
// This exists to avoid re-entrant calls into ESP-IDF image helpers from different tasks.
void device_telemetry_init();

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

// Call from the main loop to run lightweight one-shot tripwires.
// (Avoid calling from AsyncTCP/other background tasks.)
void device_telemetry_check_tripwires();

#endif // DEVICE_TELEMETRY_H
