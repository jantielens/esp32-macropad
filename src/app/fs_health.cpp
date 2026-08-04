#include "fs_health.h"

#include <string.h>

namespace {
static bool g_inited = false;
static FSHealthStats g_stats = {
		.backend = FS_BACKEND_LITTLEFS,
		.card_type = FS_CARD_TYPE_NOT_APPLICABLE,
		.storage_mounted = false,
		.storage_used_bytes = 0,
		.storage_total_bytes = 0,
};
} // namespace

void fs_health_init() {
		if (g_inited) return;
		g_inited = true;
#if USE_SD_STORAGE
		g_stats.backend = FS_BACKEND_SDMMC;
		g_stats.card_type = FS_CARD_TYPE_NONE;
#endif
}

void fs_health_set_storage_usage(uint64_t used_bytes, uint64_t total_bytes) {
		// Treat this as a one-way latch: once mounted, keep reporting mounted.
		g_stats.storage_mounted = true;
		g_stats.storage_used_bytes = used_bytes;
		g_stats.storage_total_bytes = total_bytes;
}

void fs_health_set_sd_card_type(FSCardType card_type) {
		if (!g_inited) fs_health_init();
		if (g_stats.backend != FS_BACKEND_SDMMC) return;
		g_stats.card_type = card_type;
}

void fs_health_get(FSHealthStats* out) {
		if (!g_inited) fs_health_init();
		if (!out) return;
		memcpy(out, &g_stats, sizeof(g_stats));
}
