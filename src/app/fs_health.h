#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Cached, low-overhead filesystem health information.
//
// Design goals:
// - /api/health must never mount/probe filesystems (avoid heap churn and latency)
// - Filesystem availability/type is cached at boot (partition table)
// - Usage numbers are only reported after some other subsystem has mounted the FS
//   and provided totals via fs_health_set_storage_usage().

typedef enum FSBackend {
		FS_BACKEND_LITTLEFS,
		FS_BACKEND_SDMMC,
} FSBackend;

typedef enum FSCardType {
		FS_CARD_TYPE_NOT_APPLICABLE,
		FS_CARD_TYPE_NONE,
		FS_CARD_TYPE_SD,
		FS_CARD_TYPE_SDHC,
		FS_CARD_TYPE_UNKNOWN,
} FSCardType;

typedef struct FSHealthStats {
		FSBackend backend;
		FSCardType card_type;
		bool storage_mounted;
		uint64_t storage_used_bytes;
		uint64_t storage_total_bytes;
} FSHealthStats;

void fs_health_init();

// Called by subsystems that successfully mounted the storage backend
// (LittleFS or SD_MMC). uint64_t accommodates SD cards larger than 4 GiB.
void fs_health_set_storage_usage(uint64_t used_bytes, uint64_t total_bytes);

// Records the SDMMC card type after a successful mount.
void fs_health_set_sd_card_type(FSCardType card_type);

// Returns cached stats (always succeeds after fs_health_init()).
void fs_health_get(FSHealthStats* out);

#ifdef __cplusplus
}
#endif
