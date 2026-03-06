#include "device_telemetry.h"

#include "log_manager.h"
#include "board_config.h"
#include "fs_health.h"
#include "sensors/sensor_manager.h"

#include <Arduino.h>
#include <WiFi.h>
#include "soc/soc_caps.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>
#include <freertos/idf_additions.h>
#include <esp_timer.h>

#if HAS_MQTT
#include "mqtt_manager.h"
#endif

#if HAS_DISPLAY
#include "display_manager.h"
#endif

// Temperature sensor support (ESP32-C3, ESP32-S2, ESP32-S3, ESP32-C2, ESP32-C6, ESP32-H2)
#if SOC_TEMP_SENSOR_SUPPORTED
#include "driver/temperature_sensor.h"
#endif

// Cached WiFi RSSI — sampled once at boot (and optionally on reconnect) to
// avoid repeated ESP-Hosted RPC calls from the hot health-publish path.
static int16_t s_cached_rssi = 0;
static bool    s_rssi_valid  = false;

void device_telemetry_cache_rssi() {
	if (WiFi.status() == WL_CONNECTED) {
		s_cached_rssi = WiFi.RSSI();
		s_rssi_valid  = true;
		LOGI("Telemetry", "Cached WiFi RSSI: %d dBm", (int)s_cached_rssi);
	}
}

int16_t device_telemetry_get_cached_rssi(bool *valid) {
	if (valid) *valid = s_rssi_valid;
	return s_cached_rssi;
}

// CPU usage tracking — uses FreeRTOS per-core idle-task run-time counters.
// `ulTaskGetIdleRunTimeCounterForCore()` (ESP-IDF addition) reads a single
// uint32 from the idle-task TCB — no scheduler suspension, no task-list walk.
// A 1 Hz esp_timer computes the delta and derives CPU%.
static configRUN_TIME_COUNTER_TYPE s_idle_rt_last[portNUM_PROCESSORS] = {};
static int64_t           s_wall_us_last = 0;
static volatile int      s_cpu_usage = -1;           // Written by timer, read by getter
static esp_timer_handle_t s_cpu_timer = nullptr;
static bool              s_cpu_first_sample = true;

// /api/health min/max window sampling (time-based rollover).
// Goal: capture short-lived dips/spikes without storing time series on-device.
// IMPORTANT:
// - Do NOT reset sampling on HTTP requests (multiple clients would interfere).
// - We keep a small "last" window and a "current" window and report a merged
//   snapshot, which is stable across multiple clients and makes a reasonable
//   effort to not miss spikes around rollover boundaries.
static portMUX_TYPE g_health_window_mux = portMUX_INITIALIZER_UNLOCKED;
static TimerHandle_t g_health_window_timer = nullptr;

static constexpr uint32_t kHealthWindowSamplePeriodMs = HEALTH_WINDOW_SAMPLE_PERIOD_MS;

struct HealthWindowStats {
		bool initialized;

		size_t internal_free_min;
		size_t internal_free_max;

		size_t psram_free_min;
		size_t psram_free_max;
};

static HealthWindowStats g_health_window_current = {};
static HealthWindowStats g_health_window_last = {};
static bool g_health_window_last_valid = false;

static unsigned long g_health_window_current_start_ms = 0;
static unsigned long g_health_window_last_start_ms = 0;
static unsigned long g_health_window_last_end_ms = 0;

static void health_window_reset() {
		portENTER_CRITICAL(&g_health_window_mux);
		g_health_window_current = {};
		g_health_window_last = {};
		g_health_window_last_valid = false;
		g_health_window_current_start_ms = millis();
		g_health_window_last_start_ms = 0;
		g_health_window_last_end_ms = 0;
		portEXIT_CRITICAL(&g_health_window_mux);
}

static int compute_fragmentation_percent(size_t free_bytes, size_t largest_bytes) {
		if (free_bytes == 0) return 0;
		if (largest_bytes > free_bytes) return 0;
		float frag = (1.0f - ((float)largest_bytes / (float)free_bytes)) * 100.0f;
		if (frag < 0) frag = 0;
		if (frag > 100) frag = 100;
		return (int)frag;
}

static void health_window_update_sample(size_t internal_free, size_t psram_free) {
		const unsigned long now_ms = millis();

		portENTER_CRITICAL(&g_health_window_mux);

		if (g_health_window_current_start_ms == 0) {
				g_health_window_current_start_ms = now_ms;
		}

		// Time-based rollover (shared across all clients).
		// Roll over BEFORE applying the sample so the boundary sample belongs to the new window.
		if ((uint32_t)(now_ms - g_health_window_current_start_ms) >= (uint32_t)HEALTH_POLL_INTERVAL_MS) {
				if (g_health_window_current.initialized) {
						g_health_window_last = g_health_window_current;
						g_health_window_last_valid = true;
						g_health_window_last_start_ms = g_health_window_current_start_ms;
						g_health_window_last_end_ms = now_ms;
				}

				g_health_window_current = {};
				g_health_window_current_start_ms = now_ms;
		}

		if (!g_health_window_current.initialized) {
				g_health_window_current.initialized = true;

				g_health_window_current.internal_free_min = internal_free;
				g_health_window_current.internal_free_max = internal_free;
				g_health_window_current.psram_free_min = psram_free;
				g_health_window_current.psram_free_max = psram_free;

				portEXIT_CRITICAL(&g_health_window_mux);
				return;
		}

		if (internal_free < g_health_window_current.internal_free_min) g_health_window_current.internal_free_min = internal_free;
		if (internal_free > g_health_window_current.internal_free_max) g_health_window_current.internal_free_max = internal_free;
		if (psram_free < g_health_window_current.psram_free_min) g_health_window_current.psram_free_min = psram_free;
		if (psram_free > g_health_window_current.psram_free_max) g_health_window_current.psram_free_max = psram_free;

		portEXIT_CRITICAL(&g_health_window_mux);
}

// Flash/sketch metadata caching (avoid re-entrant ESP-IDF image/mmap helpers)
static bool flash_cache_initialized = false;
static size_t cached_sketch_size = 0;
static size_t cached_free_sketch_space = 0;

// Persistent temperature sensor handle (installed once, kept enabled).
#if SOC_TEMP_SENSOR_SUPPORTED
static temperature_sensor_handle_t s_temp_sensor = nullptr;
#endif

static void fill_common(JsonDocument &doc, bool include_ip_and_channel, bool include_api_only_fields, bool include_mqtt_self_report);

static void fill_health_window_fields(JsonDocument &doc);

struct HealthWindowComputed {
		uint32_t heap_internal_free_min_window;
		uint32_t heap_internal_free_max_window;

		int heap_fragmentation_max_window;

		uint32_t psram_free_min_window;
		uint32_t psram_free_max_window;
};

static bool compute_health_window_computed(HealthWindowComputed* out);

static void health_window_get_snapshot(
		HealthWindowStats* out_last,
		bool* out_has_last,
		HealthWindowStats* out_current,
		unsigned long* out_current_start_ms,
		unsigned long* out_last_start_ms,
		unsigned long* out_last_end_ms
);

static void get_memory_snapshot(
		size_t *out_heap_free,
		size_t *out_heap_min,
		size_t *out_heap_largest,
		size_t *out_internal_free,
		size_t *out_internal_min,
		size_t *out_psram_free,
		size_t *out_psram_min,
		size_t *out_psram_largest
);

// 1 Hz timer callback — reads per-core idle-task run-time counters (microseconds)
// and derives CPU% from the delta.  Runs in esp_timer task context.
static void cpu_timer_cb(void*) {
	const int64_t wall_now = esp_timer_get_time();  // microseconds since boot

	if (s_cpu_first_sample) {
		for (int c = 0; c < portNUM_PROCESSORS; c++) {
			s_idle_rt_last[c] = ulTaskGetIdleRunTimeCounterForCore(c);
		}
		s_wall_us_last = wall_now;
		s_cpu_first_sample = false;
		return;
	}

	const int64_t wall_delta = wall_now - s_wall_us_last;
	if (wall_delta <= 0) return;

	// Sum idle microseconds across all cores.
	int64_t idle_us_total = 0;
	for (int c = 0; c < portNUM_PROCESSORS; c++) {
		const configRUN_TIME_COUNTER_TYPE cur = ulTaskGetIdleRunTimeCounterForCore(c);
		idle_us_total += (int64_t)(cur - s_idle_rt_last[c]);
		s_idle_rt_last[c] = cur;
	}
	s_wall_us_last = wall_now;

	// Total available CPU time = wall_delta * number_of_cores.
	const int64_t total_cpu_us = wall_delta * portNUM_PROCESSORS;

	int usage = (int)(100 - (idle_us_total * 100 / total_cpu_us));
	if (usage < 0)   usage = 0;
	if (usage > 100)  usage = 100;

	s_cpu_usage = usage;
}

static void health_window_timer_cb(TimerHandle_t) {
		// Pure counter reads only — no free-list walks.
		const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

		health_window_update_sample(internal_free, psram_free);
}

static void health_window_get_snapshot(
		HealthWindowStats* out_last,
		bool* out_has_last,
		HealthWindowStats* out_current,
		unsigned long* out_current_start_ms,
		unsigned long* out_last_start_ms,
		unsigned long* out_last_end_ms
) {
		if (!out_last || !out_has_last || !out_current || !out_current_start_ms || !out_last_start_ms || !out_last_end_ms) {
				return;
		}

		portENTER_CRITICAL(&g_health_window_mux);
		*out_last = g_health_window_last;
		*out_has_last = g_health_window_last_valid;
		*out_current = g_health_window_current;
		*out_current_start_ms = g_health_window_current_start_ms;
		*out_last_start_ms = g_health_window_last_start_ms;
		*out_last_end_ms = g_health_window_last_end_ms;
		portEXIT_CRITICAL(&g_health_window_mux);
}

DeviceMemorySnapshot device_telemetry_get_memory_snapshot() {
		DeviceMemorySnapshot snapshot = {};

		get_memory_snapshot(
				&snapshot.heap_free_bytes,
				&snapshot.heap_min_free_bytes,
				&snapshot.heap_largest_free_block_bytes,
				&snapshot.heap_internal_free_bytes,
				&snapshot.heap_internal_min_free_bytes,
				&snapshot.psram_free_bytes,
				&snapshot.psram_min_free_bytes,
				&snapshot.psram_largest_free_block_bytes
		);

		return snapshot;
}

void device_telemetry_log_memory_snapshot(const char *tag) {
		size_t heap_free = 0;
		size_t heap_min = 0;
		size_t heap_largest = 0;
		size_t internal_free = 0;
		size_t internal_min = 0;
		size_t psram_free = 0;
		size_t psram_min = 0;
		size_t psram_largest = 0;

		get_memory_snapshot(
				&heap_free,
				&heap_min,
				&heap_largest,
				&internal_free,
				&internal_min,
				&psram_free,
				&psram_min,
				&psram_largest
		);

		// Keep this line short to avoid fixed log buffers truncating the output.
		// Keys:
		// hf=heap_free hm=heap_min hl=heap_largest hi=internal_free hin=internal_min
		// pf=psram_free pm=psram_min pl=psram_largest
		// frag=heap fragmentation percent (based on hl/hf)

		unsigned frag_percent = 0;
		if (heap_free > 0) {
				float fragmentation = (1.0f - ((float)heap_largest / (float)heap_free)) * 100.0f;
				if (fragmentation < 0) fragmentation = 0;
				if (fragmentation > 100) fragmentation = 100;
				frag_percent = (unsigned)fragmentation;
		}

		LOGI(
				"Mem",
				"%s hf=%u hm=%u hl=%u hi=%u hin=%u frag=%u pf=%u pm=%u pl=%u",
				tag ? tag : "(null)",
				(unsigned)heap_free,
				(unsigned)heap_min,
				(unsigned)heap_largest,
				(unsigned)internal_free,
				(unsigned)internal_min,
				(unsigned)frag_percent,
				(unsigned)psram_free,
				(unsigned)psram_min,
				(unsigned)psram_largest
		);
}

void device_telemetry_fill_api(JsonDocument &doc) {
		fill_common(doc, true, true, true);

		// Min/max fields sampled by a background timer (multi-client safe).
		// We report a merged snapshot across the last complete window and the current
		// in-progress window to reduce the chance of missing short spikes around
		// rollovers without storing any time series.
		fill_health_window_fields(doc);

		// =====================================================================
		// USER-EXTEND: Add your own sensors to the web "health" API (/api/health)
		// =====================================================================
		// If you want your external sensors to show up in the web portal health widget,
		// add fields here.
		//
		// IMPORTANT:
		// - The key "cpu_temperature" is used for the SoC/internal temperature.
		//   You can safely use "temperature" for an external/ambient sensor.
		// - If you also publish these over MQTT, keep the JSON keys identical in
		//   device_telemetry_fill_mqtt() so you can reuse the same HA templates.
		//
		// Example (commented out):
		// doc["temperature"] = 23.4;
		// doc["humidity"] = 55.2;

		// Sensor framework (optional adapters)
		JsonObject sensors = doc["sensors"].to<JsonObject>();
		sensor_manager_append_api(sensors);
}

void device_telemetry_fill_mqtt_scoped(JsonDocument &doc, MqttPublishScope scope) {
		if (scope == MqttPublishScope::SensorsOnly) {
				JsonObject root = doc.to<JsonObject>();
				sensor_manager_append_mqtt(root);
				return;
		}

		// For MQTT publishing we keep the payload focused on device/system telemetry.
		// MQTT connection/publish status is better represented by availability/LWT,
		// and many consumers can infer publish cadence from broker-side timestamps.
		// Keep mqtt_* fields in /api/health only.
		fill_common(doc, false, false, false);

		// =====================================================================
		// USER-EXTEND: Add your own sensors to the MQTT state payload
		// =====================================================================
		// The MQTT integration publishes ONE batched JSON document (retained) to:
		//   devices/<sanitized>/health/state
		// Home Assistant entities then extract values via value_template, e.g.:
		//   {{ value_json.temperature }}
		//
		// Add your custom sensor fields below.
		//
		// IMPORTANT:
		// - The key "cpu_temperature" is used for the SoC/internal temperature.
		//   You can safely use "temperature" for an external/ambient sensor.
		//
		// Example (commented out):
		// doc["temperature"] = 23.4;
		// doc["humidity"] = 55.2;

		if (scope == MqttPublishScope::All) {
				JsonObject root = doc.as<JsonObject>();
				sensor_manager_append_mqtt(root);
		}
}

void device_telemetry_fill_mqtt(JsonDocument &doc) {
		device_telemetry_fill_mqtt_scoped(doc, MqttPublishScope::All);
}

void device_telemetry_init() {
		if (flash_cache_initialized) return;

		cached_sketch_size = ESP.getSketchSize();
		cached_free_sketch_space = ESP.getFreeSketchSpace();
		flash_cache_initialized = true;

#if SOC_TEMP_SENSOR_SUPPORTED
		if (!s_temp_sensor) {
				temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
				if (temperature_sensor_install(&cfg, &s_temp_sensor) == ESP_OK) {
						if (temperature_sensor_enable(s_temp_sensor) != ESP_OK) {
								temperature_sensor_uninstall(s_temp_sensor);
								s_temp_sensor = nullptr;
						}
				} else {
						s_temp_sensor = nullptr;
				}
		}
#endif
}

size_t device_telemetry_sketch_size() {
		if (!flash_cache_initialized) {
				device_telemetry_init();
		}
		return cached_sketch_size;
}

size_t device_telemetry_free_sketch_space() {
		if (!flash_cache_initialized) {
				device_telemetry_init();
		}
		return cached_free_sketch_space;
}

// Start a 1 Hz timer that reads per-core idle-task run-time counters.
void device_telemetry_start_cpu_monitoring() {
		if (s_cpu_timer != nullptr) return;  // Already started

		// Create a periodic 1 Hz esp_timer to compute the delta.
		const esp_timer_create_args_t args = {
				.callback = cpu_timer_cb,
				.arg = nullptr,
				.dispatch_method = ESP_TIMER_TASK,
				.name = "cpu_usage",
				.skip_unhandled_events = true,
		};

		esp_err_t err = esp_timer_create(&args, &s_cpu_timer);
		if (err != ESP_OK) {
				LOGE("CPU", "Failed to create timer: %s", esp_err_to_name(err));
				return;
		}

		err = esp_timer_start_periodic(s_cpu_timer, 1000000);  // 1 s
		if (err != ESP_OK) {
				LOGE("CPU", "Failed to start timer: %s", esp_err_to_name(err));
				esp_timer_delete(s_cpu_timer);
				s_cpu_timer = nullptr;
				return;
		}

		LOGI("CPU", "Run-time-stats CPU monitor started (%d core%s)",
				portNUM_PROCESSORS, portNUM_PROCESSORS > 1 ? "s" : "");
}

int device_telemetry_get_cpu_usage() {
		return s_cpu_usage;  // Atomic read of a volatile int
}

void device_telemetry_start_health_window_sampling() {
		if (g_health_window_timer != nullptr) return;

		health_window_reset();
		g_health_window_timer = xTimerCreate(
				"health_win",
				pdMS_TO_TICKS(kHealthWindowSamplePeriodMs),
				pdTRUE,
				nullptr,
				health_window_timer_cb
		);

		if (!g_health_window_timer) {
				LOGE("Health", "Failed to create health window timer");
				return;
		}

		if (xTimerStart(g_health_window_timer, 0) != pdPASS) {
				LOGE("Health", "Failed to start health window timer");
				xTimerDelete(g_health_window_timer, 0);
				g_health_window_timer = nullptr;
				return;
		}
}

static void fill_health_window_fields(JsonDocument &doc) {
		HealthWindowComputed c = {};
		if (!compute_health_window_computed(&c)) {
				return;
		}

		doc["heap_internal_free_min_window"] = c.heap_internal_free_min_window;
		doc["heap_internal_free_max_window"] = c.heap_internal_free_max_window;
		doc["heap_fragmentation_max_window"] = c.heap_fragmentation_max_window;

		doc["psram_free_min_window"] = c.psram_free_min_window;
		doc["psram_free_max_window"] = c.psram_free_max_window;
}

static bool compute_health_window_computed(HealthWindowComputed* out) {
		if (!out) return false;

		HealthWindowStats last = {};
		HealthWindowStats current = {};
		bool has_last = false;
		unsigned long current_start_ms = 0;
		unsigned long last_start_ms = 0;
		unsigned long last_end_ms = 0;

		health_window_get_snapshot(&last, &has_last, &current, &current_start_ms, &last_start_ms, &last_end_ms);

		// Fold in instantaneous request-time values to guarantee the
		// returned band contains the point-in-time fields, even between samples.
		const size_t internal_free_now = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		const size_t psram_free_now = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

		// Fragmentation computed on-demand (single free-list walk at request time, not in the 200ms timer).
		const size_t internal_largest_now = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		const int internal_frag_now = compute_fragmentation_percent(internal_free_now, internal_largest_now);

		// Merge last-complete and current-in-progress windows.
		HealthWindowStats merged = {};

		const bool has_current = current.initialized;
		const bool has_any = (has_current || (has_last && last.initialized));
		if (has_any) {
				merged.initialized = true;

				const HealthWindowStats* base = has_current ? &current : &last;
				merged.internal_free_min = base->internal_free_min;
				merged.internal_free_max = base->internal_free_max;
				merged.psram_free_min = base->psram_free_min;
				merged.psram_free_max = base->psram_free_max;

				if (has_current && has_last && last.initialized) {
						if (last.internal_free_min < merged.internal_free_min) merged.internal_free_min = last.internal_free_min;
						if (last.internal_free_max > merged.internal_free_max) merged.internal_free_max = last.internal_free_max;
						if (last.psram_free_min < merged.psram_free_min) merged.psram_free_min = last.psram_free_min;
						if (last.psram_free_max > merged.psram_free_max) merged.psram_free_max = last.psram_free_max;
				}
		}

		if (!merged.initialized) {
				merged.initialized = true;
				merged.internal_free_min = internal_free_now;
				merged.internal_free_max = internal_free_now;
				merged.psram_free_min = psram_free_now;
				merged.psram_free_max = psram_free_now;
		}

		// Guarantee instantaneous values are within the returned band.
		if (internal_free_now < merged.internal_free_min) merged.internal_free_min = internal_free_now;
		if (internal_free_now > merged.internal_free_max) merged.internal_free_max = internal_free_now;
		if (psram_free_now < merged.psram_free_min) merged.psram_free_min = psram_free_now;
		if (psram_free_now > merged.psram_free_max) merged.psram_free_max = psram_free_now;

		out->heap_internal_free_min_window = (uint32_t)merged.internal_free_min;
		out->heap_internal_free_max_window = (uint32_t)merged.internal_free_max;
		out->heap_fragmentation_max_window = internal_frag_now;
		out->psram_free_min_window = (uint32_t)merged.psram_free_min;
		out->psram_free_max_window = (uint32_t)merged.psram_free_max;

		return true;
}

bool device_telemetry_get_health_window_bands(DeviceHealthWindowBands* out_bands) {
		if (!out_bands) return false;
		HealthWindowComputed c = {};
		if (!compute_health_window_computed(&c)) return false;

		out_bands->heap_internal_free_min_window = c.heap_internal_free_min_window;
		out_bands->heap_internal_free_max_window = c.heap_internal_free_max_window;
		out_bands->psram_free_min_window = c.psram_free_min_window;
		out_bands->psram_free_max_window = c.psram_free_max_window;
		return true;
}

static void fill_common(JsonDocument &doc, bool include_ip_and_channel, bool include_api_only_fields, bool include_mqtt_self_report) {
		// System
		uint64_t uptime_us = esp_timer_get_time();
		doc["uptime_seconds"] = uptime_us / 1000000;

		// Reset reason
		esp_reset_reason_t reset_reason = esp_reset_reason();
		const char* reset_str = "Unknown";
		switch (reset_reason) {
				case ESP_RST_POWERON:   reset_str = "Power On"; break;
				case ESP_RST_SW:        reset_str = "Software"; break;
				case ESP_RST_PANIC:     reset_str = "Panic"; break;
				case ESP_RST_INT_WDT:   reset_str = "Interrupt WDT"; break;
				case ESP_RST_TASK_WDT:  reset_str = "Task WDT"; break;
				case ESP_RST_WDT:       reset_str = "WDT"; break;
				case ESP_RST_DEEPSLEEP: reset_str = "Deep Sleep"; break;
				case ESP_RST_BROWNOUT:  reset_str = "Brownout"; break;
				case ESP_RST_SDIO:      reset_str = "SDIO"; break;
				default: break;
		}
		doc["reset_reason"] = reset_str;

		// CPU usage (nullable when runtime stats are unavailable)
		const int cpu_usage = device_telemetry_get_cpu_usage();
		if (cpu_usage < 0) {
				doc["cpu_usage"] = nullptr;
		} else {
				doc["cpu_usage"] = cpu_usage;
		}

		// CPU / SoC temperature (persistent handle, installed once in device_telemetry_init)
#if SOC_TEMP_SENSOR_SUPPORTED
		{
				float temp_celsius = 0;
				if (s_temp_sensor && temperature_sensor_get_celsius(s_temp_sensor, &temp_celsius) == ESP_OK) {
						doc["cpu_temperature"] = (int)temp_celsius;
				} else {
						doc["cpu_temperature"] = nullptr;
				}
		}
#else
		doc["cpu_temperature"] = nullptr;
#endif

		// Memory — only internal heap + PSRAM free/min (no PSRAM free-list walks).
		// Mixed heap_free/heap_min removed (confusing on PSRAM boards).
		// psram_largest/psram_fragmentation removed (expensive PSRAM free-list walk).
		const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		const size_t internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		const size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

		doc["heap_internal_free"] = internal_free;
		doc["heap_internal_min"] = internal_min;
		doc["heap_internal_largest"] = internal_largest;

		// Heap fragmentation (internal only)
		float heap_frag = 0;
		if (internal_free > 0 && internal_largest <= internal_free) {
				heap_frag = (1.0f - ((float)internal_largest / (float)internal_free)) * 100.0f;
		}
		if (heap_frag < 0) heap_frag = 0;
		if (heap_frag > 100) heap_frag = 100;
		doc["heap_fragmentation"] = (int)heap_frag;

#if SOC_SPIRAM_SUPPORTED
		const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
		const size_t psram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
		doc["psram_free"] = psram_free;
		doc["psram_min"] = psram_min;
#else
		doc["psram_free"] = 0;
		doc["psram_min"] = 0;
#endif

		// Flash usage
		const size_t sketch_size = device_telemetry_sketch_size();
		const size_t free_sketch_space = device_telemetry_free_sketch_space();
		doc["flash_used"] = sketch_size;
		doc["flash_total"] = sketch_size + free_sketch_space;

		// Filesystem health (cached; may be absent or not mounted)
		{
				FSHealthStats fs;
				fs_health_get(&fs);

				if (!fs.storage_partition_present) {
						doc["fs_mounted"] = nullptr;
						doc["fs_used_bytes"] = nullptr;
						doc["fs_total_bytes"] = nullptr;
				} else {
						doc["fs_mounted"] = fs.storage_mounted ? true : false;
						if (fs.storage_mounted && fs.storage_total_bytes > 0) {
								doc["fs_used_bytes"] = (uint64_t)fs.storage_used_bytes;
								doc["fs_total_bytes"] = (uint64_t)fs.storage_total_bytes;
						} else {
								doc["fs_used_bytes"] = nullptr;
								doc["fs_total_bytes"] = nullptr;
						}
				}
		}

		// MQTT health (self-report)
		// Only included in the web API (/api/health). For MQTT consumers, availability/LWT is a better
		// source of truth, and retained state can make connection booleans misleading.
		if (include_mqtt_self_report) {
				#if HAS_MQTT
				{
						doc["mqtt_enabled"] = mqtt_manager.enabled() ? true : false;
						doc["mqtt_publish_enabled"] = mqtt_manager.publishEnabled() ? true : false;
						doc["mqtt_connected"] = mqtt_manager.connected() ? true : false;

						const unsigned long last_pub = mqtt_manager.lastHealthPublishMs();
						if (last_pub == 0) {
								doc["mqtt_last_health_publish_ms"] = nullptr;
								doc["mqtt_health_publish_age_ms"] = nullptr;
						} else {
								doc["mqtt_last_health_publish_ms"] = last_pub;
								doc["mqtt_health_publish_age_ms"] = (unsigned long)(millis() - last_pub);
						}
				}
				#else
				doc["mqtt_enabled"] = false;
				doc["mqtt_publish_enabled"] = false;
				doc["mqtt_connected"] = false;
				doc["mqtt_last_health_publish_ms"] = nullptr;
				doc["mqtt_health_publish_age_ms"] = nullptr;
				#endif
		}

		// Display FPS (API-only; excluded from MQTT payload).
	if (include_api_only_fields) {
				#if HAS_DISPLAY
				if (displayManager) {
						DisplayPerfStats stats;
						if (display_manager_get_perf_stats(&stats)) {
								doc["display_fps"] = stats.fps;
						} else {
								doc["display_fps"] = nullptr;
						}
				} else {
						doc["display_fps"] = nullptr;
				}
				#else
				doc["display_fps"] = nullptr;
				#endif
		}

// WiFi stats — use cached RSSI to avoid ESP-Hosted RPC on every publish.
	if (WiFi.status() == WL_CONNECTED) {
			bool rssi_ok = false;
			int16_t rssi = device_telemetry_get_cached_rssi(&rssi_ok);
			doc["wifi_rssi"] = rssi_ok ? (int)rssi : (int)0;

				if (include_ip_and_channel) {
						doc["wifi_channel"] = WiFi.channel();

						// Avoid heap churn in String::toString() by formatting into a fixed buffer.
						char ip_buf[16];
						snprintf(ip_buf, sizeof(ip_buf), "%u.%u.%u.%u", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
						doc["ip_address"] = ip_buf;

						doc["hostname"] = WiFi.getHostname();
				}
		} else {
				doc["wifi_rssi"] = nullptr;

				if (include_ip_and_channel) {
						doc["wifi_channel"] = nullptr;
						doc["ip_address"] = nullptr;
						doc["hostname"] = nullptr;
				}
		}
}

static void get_memory_snapshot(
		size_t *out_heap_free,
		size_t *out_heap_min,
		size_t *out_heap_largest,
		size_t *out_internal_free,
		size_t *out_internal_min,
		size_t *out_psram_free,
		size_t *out_psram_min,
		size_t *out_psram_largest
) {
		if (out_heap_free) *out_heap_free = ESP.getFreeHeap();
		if (out_heap_min) *out_heap_min = ESP.getMinFreeHeap();

		if (out_heap_largest) {
				// Keep this consistent with ESP.getFreeHeap() (internal heap): use INTERNAL 8-bit largest block.
				*out_heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		}

		if (out_internal_free) {
				*out_internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		}
		if (out_internal_min) {
				*out_internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		}

#if SOC_SPIRAM_SUPPORTED
		if (out_psram_free) {
				*out_psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
		}
		if (out_psram_min) {
				*out_psram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
		}
		if (out_psram_largest) {
				*out_psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
		}
#else
		if (out_psram_free) *out_psram_free = 0;
		if (out_psram_min) *out_psram_min = 0;
		if (out_psram_largest) *out_psram_largest = 0;
#endif
}