#pragma once

#include <stddef.h>
#include <stdint.h>

// Device-side health history ring buffer used by /api/health/history.
// Enabled via HEALTH_HISTORY_ENABLED.

struct HealthHistoryParams {
		uint32_t period_ms;
		uint32_t seconds;
		uint32_t samples;
};

struct HealthHistorySample {
		uint32_t uptime_ms;

		int16_t cpu_usage; // -1 => unknown

		uint32_t heap_internal_free;
		uint32_t heap_internal_free_min_window;
		uint32_t heap_internal_free_max_window;

		uint32_t psram_free;
		uint32_t psram_free_min_window;
		uint32_t psram_free_max_window;

		uint32_t heap_internal_largest;
		uint32_t heap_internal_largest_min_window;
		uint32_t heap_internal_largest_max_window;
};

// Starts background sampling if enabled. Safe to call multiple times.
void health_history_start();

// Returns whether device-side history is enabled and initialized.
bool health_history_available();

// Returns the configured parameters (all zeros when unavailable).
HealthHistoryParams health_history_params();

// Returns the number of valid samples currently stored.
size_t health_history_count();

// Returns the capacity of the ring buffer.
size_t health_history_capacity();

// Copy the i-th oldest sample (0..count-1). Returns false if out of range/unavailable.
bool health_history_get_sample(size_t index, HealthHistorySample* out_sample);
