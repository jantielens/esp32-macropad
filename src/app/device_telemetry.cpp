#include "device_telemetry.h"

#include "log_manager.h"
#include "board_config.h"
#include "fs_health.h"
#include "rtos_task_utils.h"
#include "sensors/sensor_manager.h"

#include <Arduino.h>
#include <WiFi.h>
#include "soc/soc_caps.h"
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>

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

// CPU usage tracking (task-based)
static SemaphoreHandle_t cpu_mutex = nullptr;
static int cpu_usage_current = -1;
static TaskHandle_t cpu_task_handle = nullptr;
static RtosTaskPsramAlloc cpu_task_alloc = {};

// Delta tracking for calculation
static uint32_t last_idle_runtime = 0;
static uint32_t last_total_runtime = 0;
static bool first_calculation = true;

static bool log_every_ms(unsigned long now_ms, unsigned long *last_ms, unsigned long interval_ms) {
		if (!last_ms) return false;
		if (*last_ms == 0 || (now_ms - *last_ms) >= interval_ms) {
				*last_ms = now_ms;
				return true;
		}
		return false;
}

// /api/health min/max window sampling (time-based rollover).
// Goal: capture short-lived dips/spikes without storing time series on-device.
// IMPORTANT:
// - Do NOT reset sampling on HTTP requests (multiple clients would interfere).
// - We keep a small "last" window and a "current" window and report a merged
//   snapshot, which is stable across multiple clients and makes a reasonable
//   effort to not miss spikes around rollover boundaries.
static portMUX_TYPE g_health_window_mux = portMUX_INITIALIZER_UNLOCKED;
static TimerHandle_t g_health_window_timer = nullptr;

static constexpr uint32_t kHealthWindowSamplePeriodMs = 200;

struct HealthWindowStats {
		bool initialized;

		size_t internal_free_min;
		size_t internal_free_max;
		size_t internal_largest_min;
		size_t internal_largest_max;
		int internal_frag_max;

		size_t psram_free_min;
		size_t psram_free_max;
		size_t psram_largest_min;
		int psram_frag_max;
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

static void health_window_update_sample(size_t internal_free, size_t internal_largest, size_t psram_free, size_t psram_largest) {
		const int internal_frag = compute_fragmentation_percent(internal_free, internal_largest);
		const int psram_frag = compute_fragmentation_percent(psram_free, psram_largest);

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
				g_health_window_current.internal_largest_min = internal_largest;
				g_health_window_current.internal_largest_max = internal_largest;
				g_health_window_current.internal_frag_max = internal_frag;

				g_health_window_current.psram_free_min = psram_free;
				g_health_window_current.psram_free_max = psram_free;
				g_health_window_current.psram_largest_min = psram_largest;
				g_health_window_current.psram_frag_max = psram_frag;

				portEXIT_CRITICAL(&g_health_window_mux);
				return;
		}

		if (internal_free < g_health_window_current.internal_free_min) g_health_window_current.internal_free_min = internal_free;
		if (internal_free > g_health_window_current.internal_free_max) g_health_window_current.internal_free_max = internal_free;
		if (internal_largest < g_health_window_current.internal_largest_min) g_health_window_current.internal_largest_min = internal_largest;
		if (internal_largest > g_health_window_current.internal_largest_max) g_health_window_current.internal_largest_max = internal_largest;
		if (internal_frag > g_health_window_current.internal_frag_max) g_health_window_current.internal_frag_max = internal_frag;

		if (psram_free < g_health_window_current.psram_free_min) g_health_window_current.psram_free_min = psram_free;
		if (psram_free > g_health_window_current.psram_free_max) g_health_window_current.psram_free_max = psram_free;
		if (psram_largest < g_health_window_current.psram_largest_min) g_health_window_current.psram_largest_min = psram_largest;
		if (psram_frag > g_health_window_current.psram_frag_max) g_health_window_current.psram_frag_max = psram_frag;

		portEXIT_CRITICAL(&g_health_window_mux);
}

// Flash/sketch metadata caching (avoid re-entrant ESP-IDF image/mmap helpers)
static bool flash_cache_initialized = false;
static size_t cached_sketch_size = 0;
static size_t cached_free_sketch_space = 0;

static void fill_common(JsonDocument &doc, bool include_ip_and_channel, bool include_debug_fields, bool include_mqtt_self_report);

static void fill_health_window_fields(JsonDocument &doc);

struct HealthWindowComputed {
		uint32_t heap_internal_free_min_window;
		uint32_t heap_internal_free_max_window;

		uint32_t heap_internal_largest_min_window;
		uint32_t heap_internal_largest_max_window;
		int heap_fragmentation_max_window;

		uint32_t psram_free_min_window;
		uint32_t psram_free_max_window;
		uint32_t psram_largest_min_window;
		int psram_fragmentation_max_window;
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

static int calculate_cpu_usage() {
		// IMPORTANT:
		// - uxTaskGetSystemState returns 0 when the provided array is too small.
		// - TaskStatus_t is fairly large; keep this out of stack (CPU monitor task stack is small).
		// - If runtime stats aren't enabled, total_runtime and ulRunTimeCounter stay 0 -> treat as unknown.
		constexpr UBaseType_t kMaxTasks = 24;
		static TaskStatus_t task_stats[kMaxTasks];

		// Guard: if there are more tasks than we can sample, bail out and log.
		// This keeps the static array size fixed (no extra RAM), while making truncation visible.
		static unsigned long last_truncation_log_ms = 0;
		const unsigned long now_ms = millis();
		const UBaseType_t expected_tasks = uxTaskGetNumberOfTasks();
		if (expected_tasks > kMaxTasks) {
				if (log_every_ms(now_ms, &last_truncation_log_ms, 5000)) {
						LOGI(
								"CPU",
								"Runtime stats truncated: tasks=%u > max=%u (cpu_usage unavailable)",
								(unsigned)expected_tasks,
								(unsigned)kMaxTasks
						);
				}
				return -1;
		}

		uint32_t total_runtime = 0;
		const int task_count = uxTaskGetSystemState(task_stats, kMaxTasks, &total_runtime);

		static unsigned long last_runtime_stats_log_ms = 0;
		if (task_count <= 0 || total_runtime == 0) {
				if (log_every_ms(now_ms, &last_runtime_stats_log_ms, 5000)) {
						LOGI(
								"CPU",
								"Runtime stats unavailable: uxTaskGetSystemState=%d total_runtime=%lu",
								task_count,
								(unsigned long)total_runtime
						);
				}
				return -1;
		}

		// Count IDLE tasks and sum their runtimes.
		uint32_t idle_runtime = 0;
		int idle_task_count = 0;
		for (UBaseType_t i = 0; i < task_count; i++) {
				const char* name = task_stats[i].pcTaskName;
				if (name && strstr(name, "IDLE") != nullptr) {
						idle_runtime += task_stats[i].ulRunTimeCounter;
						idle_task_count++;
				}
		}

		if (idle_task_count <= 0) return -1;

		// Skip first calculation (need delta)
		if (first_calculation) {
				last_idle_runtime = idle_runtime;
				last_total_runtime = total_runtime;
				first_calculation = false;
				return -1;
		}

		// Calculate delta.
		const uint32_t idle_delta = idle_runtime - last_idle_runtime;
		const uint32_t total_delta = total_runtime - last_total_runtime;

		last_idle_runtime = idle_runtime;
		last_total_runtime = total_runtime;

		if (total_delta == 0) return -1;

		const uint32_t max_idle_time = total_delta * (uint32_t)idle_task_count;
		if (max_idle_time == 0) return -1;

		const float idle_percent = ((float)idle_delta / (float)max_idle_time) * 100.0f;
		int cpu_usage = (int)(100.0f - idle_percent);

		if (cpu_usage < 0) cpu_usage = 0;
		if (cpu_usage > 100) cpu_usage = 100;

		return cpu_usage;
}

static void health_window_timer_cb(TimerHandle_t) {
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

		// heap_largest is computed as INTERNAL largest free block (see get_memory_snapshot).
		health_window_update_sample(internal_free, heap_largest, psram_free, psram_largest);
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

static void log_task_stack_watermarks_one_shot() {
		const UBaseType_t task_count = uxTaskGetNumberOfTasks();
		if (task_count == 0) return;

		TaskStatus_t* tasks = nullptr;
		if (psramFound()) {
				tasks = (TaskStatus_t*)heap_caps_malloc(sizeof(TaskStatus_t) * task_count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		}
		if (!tasks) {
				tasks = (TaskStatus_t*)heap_caps_malloc(sizeof(TaskStatus_t) * task_count, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		}
		if (!tasks) {
				LOGE("Mem", "TRIPWIRE: OOM while allocating task list");
				return;
		}

		uint32_t total_runtime = 0;
		const UBaseType_t got = uxTaskGetSystemState(tasks, task_count, &total_runtime);
		if (got == 0) {
				heap_caps_free(tasks);
				LOGE("Mem", "TRIPWIRE: uxTaskGetSystemState returned 0");
				return;
		}

		// Sort by lowest high-water mark first (most at risk).
		for (UBaseType_t i = 0; i < got; i++) {
				UBaseType_t best = i;
				for (UBaseType_t j = i + 1; j < got; j++) {
						if (tasks[j].usStackHighWaterMark < tasks[best].usStackHighWaterMark) {
								best = j;
						}
				}
				if (best != i) {
						TaskStatus_t tmp = tasks[i];
						tasks[i] = tasks[best];
						tasks[best] = tmp;
				}
		}

		// Keep logs bounded.
		const UBaseType_t max_to_log = (got > 16) ? 16 : got;
		LOGI("Mem", "TRIPWIRE: task stack watermarks (worst %u/%u)", (unsigned)max_to_log, (unsigned)got);
		for (UBaseType_t i = 0; i < max_to_log; i++) {
				const unsigned bytes = (unsigned)tasks[i].usStackHighWaterMark * (unsigned)sizeof(StackType_t);
				LOGI("Stack", "%s hw=%u", tasks[i].pcTaskName ? tasks[i].pcTaskName : "(null)", bytes);
		}

		heap_caps_free(tasks);
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

void device_telemetry_check_tripwires() {
		#if MEMORY_TRIPWIRE_INTERNAL_MIN_BYTES == 0
		return;
		#else
		static bool fired = false;
		static unsigned long last_check = 0;

		if (fired) return;

		const unsigned long now = millis();
		if (last_check != 0 && (now - last_check) < (unsigned long)MEMORY_TRIPWIRE_CHECK_INTERVAL_MS) {
				return;
		}
		last_check = now;

		DeviceMemorySnapshot snapshot = device_telemetry_get_memory_snapshot();
		if (snapshot.heap_internal_min_free_bytes > (size_t)MEMORY_TRIPWIRE_INTERNAL_MIN_BYTES) {
				return;
		}

		fired = true;
		LOGI(
				"Mem",
				"TRIPWIRE fired: internal_min=%u <= %u",
				(unsigned)snapshot.heap_internal_min_free_bytes,
				(unsigned)MEMORY_TRIPWIRE_INTERNAL_MIN_BYTES
		);
		log_task_stack_watermarks_one_shot();
		#endif
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

// Background task: Calculate CPU usage every 1s.
static void cpu_monitoring_task(void* param) {
		while (true) {
				const int new_value = calculate_cpu_usage();

				xSemaphoreTake(cpu_mutex, portMAX_DELAY);
				cpu_usage_current = new_value;
				xSemaphoreGive(cpu_mutex);

				vTaskDelay(pdMS_TO_TICKS(1000));
		}
}

void device_telemetry_start_cpu_monitoring() {
		if (cpu_task_handle != nullptr) return;  // Already started
		
		cpu_mutex = xSemaphoreCreateMutex();
		if (cpu_mutex == nullptr) {
				LOGE("CPU", "Failed to create mutex");
				return;
		}
		
#if SOC_SPIRAM_SUPPORTED
		if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
				const bool ok = rtos_create_task_psram_stack(
						cpu_monitoring_task,
						"cpu_monitor",
						2048,  // Stack depth (words)
						nullptr,
						1,  // Low priority
						&cpu_task_handle,
						&cpu_task_alloc
				);

				if (!ok) {
						LOGE("CPU", "Failed to create PSRAM-backed cpu_monitor task");
						abort();
				}

				LOGI("CPU", "Created task with PSRAM-backed stack");
				return;
		}
#endif

		BaseType_t result = xTaskCreate(
				cpu_monitoring_task,
				"cpu_monitor",
				2048,  // Stack depth (words)
				nullptr,
				1,  // Low priority
				&cpu_task_handle
		);

		if (result != pdPASS) {
				LOGE("CPU", "Failed to create task");
				vSemaphoreDelete(cpu_mutex);
				cpu_mutex = nullptr;
		}
}

int device_telemetry_get_cpu_usage() {
		if (cpu_mutex == nullptr) return -1;  // Not initialized

		int value = -1;
		xSemaphoreTake(cpu_mutex, portMAX_DELAY);
		value = cpu_usage_current;
		xSemaphoreGive(cpu_mutex);
		return value;
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
		doc["heap_internal_largest_min_window"] = c.heap_internal_largest_min_window;
		doc["heap_internal_largest_max_window"] = c.heap_internal_largest_max_window;
		doc["heap_fragmentation_max_window"] = c.heap_fragmentation_max_window;

		doc["psram_free_min_window"] = c.psram_free_min_window;
		doc["psram_free_max_window"] = c.psram_free_max_window;
		doc["psram_largest_min_window"] = c.psram_largest_min_window;
		doc["psram_fragmentation_max_window"] = c.psram_fragmentation_max_window;
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

		// Also fold in instantaneous request-time values to guarantee the returned
		// band contains the point-in-time fields, even between 200ms samples.
		size_t heap_free_now = 0;
		size_t heap_min_now = 0;
		size_t heap_largest_now = 0;
		size_t internal_free_now = 0;
		size_t internal_min_now = 0;
		size_t psram_free_now = 0;
		size_t psram_min_now = 0;
		size_t psram_largest_now = 0;
		get_memory_snapshot(
				&heap_free_now,
				&heap_min_now,
				&heap_largest_now,
				&internal_free_now,
				&internal_min_now,
				&psram_free_now,
				&psram_min_now,
				&psram_largest_now
		);
		const int internal_frag_now = compute_fragmentation_percent(internal_free_now, heap_largest_now);
		const int psram_frag_now = compute_fragmentation_percent(psram_free_now, psram_largest_now);

		// Merge last-complete and current-in-progress windows.
		// This is conservative (can be slightly wider than a strict "last N seconds" window),
		// but avoids missing spikes without extra RAM.
		HealthWindowStats merged = {};

		const bool has_current = current.initialized;
		const bool has_any = (has_current || (has_last && last.initialized));
		if (has_any) {
				merged.initialized = true;

				// Start from whichever window is present.
				const HealthWindowStats* base = has_current ? &current : &last;
				merged.internal_free_min = base->internal_free_min;
				merged.internal_free_max = base->internal_free_max;
				merged.internal_largest_min = base->internal_largest_min;
				merged.internal_largest_max = base->internal_largest_max;
				merged.internal_frag_max = base->internal_frag_max;

				merged.psram_free_min = base->psram_free_min;
				merged.psram_free_max = base->psram_free_max;
				merged.psram_largest_min = base->psram_largest_min;
				merged.psram_frag_max = base->psram_frag_max;

				if (has_current && has_last && last.initialized) {
						if (last.internal_free_min < merged.internal_free_min) merged.internal_free_min = last.internal_free_min;
						if (last.internal_free_max > merged.internal_free_max) merged.internal_free_max = last.internal_free_max;
						if (last.internal_largest_min < merged.internal_largest_min) merged.internal_largest_min = last.internal_largest_min;
						if (last.internal_largest_max > merged.internal_largest_max) merged.internal_largest_max = last.internal_largest_max;
						if (last.internal_frag_max > merged.internal_frag_max) merged.internal_frag_max = last.internal_frag_max;

						if (last.psram_free_min < merged.psram_free_min) merged.psram_free_min = last.psram_free_min;
						if (last.psram_free_max > merged.psram_free_max) merged.psram_free_max = last.psram_free_max;
						if (last.psram_largest_min < merged.psram_largest_min) merged.psram_largest_min = last.psram_largest_min;
						if (last.psram_frag_max > merged.psram_frag_max) merged.psram_frag_max = last.psram_frag_max;
				}
		}

		if (!merged.initialized) {
				// Early boot: initialize from instantaneous values.
				merged.initialized = true;
				merged.internal_free_min = internal_free_now;
				merged.internal_free_max = internal_free_now;
				merged.internal_largest_min = heap_largest_now;
				merged.internal_largest_max = heap_largest_now;
				merged.internal_frag_max = internal_frag_now;

				merged.psram_free_min = psram_free_now;
				merged.psram_free_max = psram_free_now;
				merged.psram_largest_min = psram_largest_now;
				merged.psram_frag_max = psram_frag_now;
		}

		// Guarantee the instantaneous request-time values are within the returned band.
		if (internal_free_now < merged.internal_free_min) merged.internal_free_min = internal_free_now;
		if (internal_free_now > merged.internal_free_max) merged.internal_free_max = internal_free_now;
		if (heap_largest_now < merged.internal_largest_min) merged.internal_largest_min = heap_largest_now;
		if (heap_largest_now > merged.internal_largest_max) merged.internal_largest_max = heap_largest_now;
		if (internal_frag_now > merged.internal_frag_max) merged.internal_frag_max = internal_frag_now;

		if (psram_free_now < merged.psram_free_min) merged.psram_free_min = psram_free_now;
		if (psram_free_now > merged.psram_free_max) merged.psram_free_max = psram_free_now;
		if (psram_largest_now < merged.psram_largest_min) merged.psram_largest_min = psram_largest_now;
		if (psram_frag_now > merged.psram_frag_max) merged.psram_frag_max = psram_frag_now;

		out->heap_internal_free_min_window = (uint32_t)merged.internal_free_min;
		out->heap_internal_free_max_window = (uint32_t)merged.internal_free_max;

		out->heap_internal_largest_min_window = (uint32_t)merged.internal_largest_min;
		out->heap_internal_largest_max_window = (uint32_t)merged.internal_largest_max;
		out->heap_fragmentation_max_window = merged.internal_frag_max;

		out->psram_free_min_window = (uint32_t)merged.psram_free_min;
		out->psram_free_max_window = (uint32_t)merged.psram_free_max;
		out->psram_largest_min_window = (uint32_t)merged.psram_largest_min;
		out->psram_fragmentation_max_window = merged.psram_frag_max;

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
		out_bands->heap_internal_largest_min_window = c.heap_internal_largest_min_window;
		out_bands->heap_internal_largest_max_window = c.heap_internal_largest_max_window;
		return true;
}

static void fill_common(JsonDocument &doc, bool include_ip_and_channel, bool include_debug_fields, bool include_mqtt_self_report) {
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

		// CPU (API includes cpu_freq; MQTT keeps payload smaller)
		if (include_debug_fields) {
				doc["cpu_freq"] = ESP.getCpuFreqMHz();
		}

		// CPU usage (nullable when runtime stats are unavailable)
		const int cpu_usage = device_telemetry_get_cpu_usage();
		if (cpu_usage < 0) {
				doc["cpu_usage"] = nullptr;
		} else {
				doc["cpu_usage"] = cpu_usage;
		}

		// CPU / SoC temperature
#if SOC_TEMP_SENSOR_SUPPORTED
		float temp_celsius = 0;
		temperature_sensor_handle_t temp_sensor = NULL;
		temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);

		if (temperature_sensor_install(&temp_sensor_config, &temp_sensor) == ESP_OK) {
				if (temperature_sensor_enable(temp_sensor) == ESP_OK) {
						if (temperature_sensor_get_celsius(temp_sensor, &temp_celsius) == ESP_OK) {
								doc["cpu_temperature"] = (int)temp_celsius;
						} else {
								doc["cpu_temperature"] = nullptr;
						}
						temperature_sensor_disable(temp_sensor);
				} else {
						doc["cpu_temperature"] = nullptr;
				}
				temperature_sensor_uninstall(temp_sensor);
		} else {
				doc["cpu_temperature"] = nullptr;
		}
#else
		doc["cpu_temperature"] = nullptr;
#endif

		// Memory
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

		doc["heap_free"] = heap_free;
		doc["heap_min"] = heap_min;
		if (include_debug_fields) {
				doc["heap_size"] = ESP.getHeapSize();
		}

		// Additional heap/PSRAM details (useful for memory/fragmentation investigations)
		doc["heap_largest"] = heap_largest;
		doc["heap_internal_free"] = internal_free;
		doc["heap_internal_min"] = internal_min;
		const size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		doc["heap_internal_largest"] = internal_largest;
		doc["psram_free"] = psram_free;
		doc["psram_min"] = psram_min;
		doc["psram_largest"] = psram_largest;

		// Heap fragmentation
		// IMPORTANT: On PSRAM boards, `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` can return a PSRAM block,
		// while `ESP.getFreeHeap()` reports internal heap only. Mixing those yields negative fragmentation.
		// We define heap fragmentation as INTERNAL heap fragmentation.
		float heap_frag = 0;
		if (internal_free > 0 && internal_largest <= internal_free) {
				heap_frag = (1.0f - ((float)internal_largest / (float)internal_free)) * 100.0f;
		}
		if (heap_frag < 0) heap_frag = 0;
		if (heap_frag > 100) heap_frag = 100;
		doc["heap_fragmentation"] = (int)heap_frag;

		float psram_frag = 0;
		if (psram_free > 0 && psram_largest <= psram_free) {
				psram_frag = (1.0f - ((float)psram_largest / (float)psram_free)) * 100.0f;
		}
		if (psram_frag < 0) psram_frag = 0;
		if (psram_frag > 100) psram_frag = 100;
		doc["psram_fragmentation"] = (int)psram_frag;

		// Flash usage
		const size_t sketch_size = device_telemetry_sketch_size();
		const size_t free_sketch_space = device_telemetry_free_sketch_space();
		doc["flash_used"] = sketch_size;
		doc["flash_total"] = sketch_size + free_sketch_space;

		// Filesystem health (cached; may be absent or not mounted)
		{
				FSHealthStats fs;
				fs_health_get(&fs);

				if (!fs.ffat_partition_present) {
						doc["fs_mounted"] = nullptr;
						doc["fs_used_bytes"] = nullptr;
						doc["fs_total_bytes"] = nullptr;
				} else {
						doc["fs_mounted"] = fs.ffat_mounted ? true : false;
						if (fs.ffat_mounted && fs.ffat_total_bytes > 0) {
								doc["fs_used_bytes"] = (uint64_t)fs.ffat_used_bytes;
								doc["fs_total_bytes"] = (uint64_t)fs.ffat_total_bytes;
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

		// Display perf (best-effort)
		#if HAS_DISPLAY
		if (displayManager) {
				DisplayPerfStats stats;
				if (display_manager_get_perf_stats(&stats)) {
						doc["display_fps"] = stats.fps;
						doc["display_lv_timer_us"] = stats.lv_timer_us;
						doc["display_present_us"] = stats.present_us;
				} else {
						doc["display_fps"] = nullptr;
						doc["display_lv_timer_us"] = nullptr;
						doc["display_present_us"] = nullptr;
				}
		} else {
				doc["display_fps"] = nullptr;
				doc["display_lv_timer_us"] = nullptr;
				doc["display_present_us"] = nullptr;
		}
		#else
		doc["display_fps"] = nullptr;
		doc["display_lv_timer_us"] = nullptr;
		doc["display_present_us"] = nullptr;
		#endif

		// WiFi stats (only if connected)
		if (WiFi.status() == WL_CONNECTED) {
				doc["wifi_rssi"] = WiFi.RSSI();

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