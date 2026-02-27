#include "health_history.h"

#include "board_config.h"
#include "device_telemetry.h"
#include "log_manager.h"

#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#if ESP32
#include <esp_heap_caps.h>
#endif

#if HEALTH_HISTORY_ENABLED

static portMUX_TYPE g_hist_mux = portMUX_INITIALIZER_UNLOCKED;
static TimerHandle_t g_hist_timer = nullptr;
static HealthHistorySample* g_hist_samples = nullptr;
static size_t g_hist_capacity = 0;
static size_t g_hist_head = 0; // next write index
static size_t g_hist_count = 0;

static void* hist_alloc(size_t bytes) {
		if (bytes == 0) return nullptr;

#if SOC_SPIRAM_SUPPORTED
		if (ESP.getPsramSize() > 0) {
				void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
				if (p) return p;
		}
#endif

#if ESP32
		// Prefer internal heap but allow fallback.
		void* p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		if (p) return p;
#endif

		return malloc(bytes);
}

static void hist_free(void* p) {
		if (!p) return;
#if ESP32
		heap_caps_free(p);
#else
		free(p);
#endif
}

static void hist_write_sample(const HealthHistorySample& s) {
		portENTER_CRITICAL(&g_hist_mux);

		if (g_hist_samples && g_hist_capacity > 0) {
				g_hist_samples[g_hist_head] = s;
				g_hist_head = (g_hist_head + 1) % g_hist_capacity;
				if (g_hist_count < g_hist_capacity) {
						g_hist_count++;
				}
		}

		portEXIT_CRITICAL(&g_hist_mux);
}

static void hist_timer_cb(TimerHandle_t) {
		HealthHistorySample s = {};

		s.uptime_ms = (uint32_t)millis();

		const int cpu_usage = device_telemetry_get_cpu_usage();
		s.cpu_usage = (cpu_usage < 0) ? (int16_t)-1 : (int16_t)cpu_usage;

		const DeviceMemorySnapshot mem = device_telemetry_get_memory_snapshot();
		s.heap_internal_free = (uint32_t)mem.heap_internal_free_bytes;
		s.psram_free = (uint32_t)mem.psram_free_bytes;

		// For consistency with /api/health, treat this as internal largest block.
		s.heap_internal_largest = (uint32_t)mem.heap_largest_free_block_bytes;

		DeviceHealthWindowBands bands = {};
		if (device_telemetry_get_health_window_bands(&bands)) {
				s.heap_internal_free_min_window = bands.heap_internal_free_min_window;
				s.heap_internal_free_max_window = bands.heap_internal_free_max_window;

				s.psram_free_min_window = bands.psram_free_min_window;
				s.psram_free_max_window = bands.psram_free_max_window;

				s.heap_internal_largest_min_window = bands.heap_internal_largest_min_window;
				s.heap_internal_largest_max_window = bands.heap_internal_largest_max_window;
		} else {
				// Early boot fallback: use instantaneous values as a degenerate band.
				s.heap_internal_free_min_window = s.heap_internal_free;
				s.heap_internal_free_max_window = s.heap_internal_free;

				s.psram_free_min_window = s.psram_free;
				s.psram_free_max_window = s.psram_free;

				s.heap_internal_largest_min_window = s.heap_internal_largest;
				s.heap_internal_largest_max_window = s.heap_internal_largest;
		}

		hist_write_sample(s);
}

void health_history_start() {
		if (g_hist_timer != nullptr) return;

		g_hist_capacity = (size_t)HEALTH_HISTORY_SAMPLES;
		g_hist_head = 0;
		g_hist_count = 0;

		const size_t bytes = g_hist_capacity * sizeof(HealthHistorySample);
		g_hist_samples = (HealthHistorySample*)hist_alloc(bytes);

		if (!g_hist_samples) {
				LOGE("HealthHist", "Failed to allocate history buffer");
				g_hist_capacity = 0;
				return;
		}

		memset(g_hist_samples, 0, bytes);

		g_hist_timer = xTimerCreate(
				"health_hist",
				pdMS_TO_TICKS((uint32_t)HEALTH_HISTORY_PERIOD_MS),
				pdTRUE,
				nullptr,
				hist_timer_cb
		);

		if (!g_hist_timer) {
				LOGE("HealthHist", "Failed to create history timer");
				hist_free(g_hist_samples);
				g_hist_samples = nullptr;
				g_hist_capacity = 0;
				return;
		}

		if (xTimerStart(g_hist_timer, 0) != pdPASS) {
				LOGE("HealthHist", "Failed to start history timer");
				xTimerDelete(g_hist_timer, 0);
				g_hist_timer = nullptr;
				hist_free(g_hist_samples);
				g_hist_samples = nullptr;
				g_hist_capacity = 0;
				return;
		}

		// Take an immediate first sample so UI has data quickly.
		hist_timer_cb(nullptr);

		LOGI("HealthHist", "Enabled: %u samples @ %u ms (~%u bytes)",
				(unsigned)g_hist_capacity,
				(unsigned)HEALTH_HISTORY_PERIOD_MS,
				(unsigned)bytes
		);
}

bool health_history_available() {
		return (g_hist_timer != nullptr) && (g_hist_samples != nullptr) && (g_hist_capacity > 0);
}

HealthHistoryParams health_history_params() {
		HealthHistoryParams p = {};
		if (!health_history_available()) return p;
		p.period_ms = (uint32_t)HEALTH_HISTORY_PERIOD_MS;
		p.seconds = (uint32_t)HEALTH_HISTORY_SECONDS;
		p.samples = (uint32_t)g_hist_capacity;
		return p;
}

size_t health_history_count() {
		portENTER_CRITICAL(&g_hist_mux);
		const size_t c = g_hist_count;
		portEXIT_CRITICAL(&g_hist_mux);
		return c;
}

size_t health_history_capacity() {
		return g_hist_capacity;
}

bool health_history_get_sample(size_t index, HealthHistorySample* out_sample) {
		if (!out_sample) return false;
		if (!health_history_available()) return false;

		portENTER_CRITICAL(&g_hist_mux);
		const size_t c = g_hist_count;
		const size_t cap = g_hist_capacity;
		const size_t head = g_hist_head;
		if (index >= c || cap == 0) {
				portEXIT_CRITICAL(&g_hist_mux);
				return false;
		}

		// Oldest sample index in the ring.
		const size_t oldest = (head + cap - c) % cap;
		const size_t pos = (oldest + index) % cap;
		*out_sample = g_hist_samples[pos];

		portEXIT_CRITICAL(&g_hist_mux);
		return true;
}

#else

void health_history_start() {}

bool health_history_available() { return false; }

HealthHistoryParams health_history_params() {
		HealthHistoryParams p = {};
		return p;
}

size_t health_history_count() { return 0; }

size_t health_history_capacity() { return 0; }

bool health_history_get_sample(size_t, HealthHistorySample*) { return false; }

#endif
