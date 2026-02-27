#include "fs_health.h"

#include <string.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_partition.h>
#endif

namespace {
static bool g_inited = false;
static FSHealthStats g_stats = {
		.ffat_partition_present = false,
		.ffat_mounted = false,
		.ffat_used_bytes = 0,
		.ffat_total_bytes = 0,
};

static void detect_partitions() {
#if defined(ARDUINO_ARCH_ESP32)
		const esp_partition_t* ffat_part = esp_partition_find_first(
				ESP_PARTITION_TYPE_DATA,
				ESP_PARTITION_SUBTYPE_DATA_FAT,
				nullptr);
		g_stats.ffat_partition_present = (ffat_part != nullptr);
#else
		g_stats.ffat_partition_present = false;
#endif
}
} // namespace

void fs_health_init() {
		if (g_inited) return;
		g_inited = true;
		detect_partitions();
}

void fs_health_set_ffat_usage(uint32_t used_bytes, uint32_t total_bytes) {
		// Treat this as a one-way latch: once mounted, keep reporting mounted.
		g_stats.ffat_mounted = true;
		g_stats.ffat_used_bytes = used_bytes;
		g_stats.ffat_total_bytes = total_bytes;
}

void fs_health_get(FSHealthStats* out) {
		if (!g_inited) fs_health_init();
		if (!out) return;
		memcpy(out, &g_stats, sizeof(g_stats));
}
