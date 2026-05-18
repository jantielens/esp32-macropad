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

typedef struct FSHealthStats {
		bool storage_partition_present;
		bool storage_mounted;
		uint64_t storage_used_bytes;
		uint64_t storage_total_bytes;
} FSHealthStats;

void fs_health_init();

// Called by subsystems that successfully mounted the storage backend
// (LittleFS or SD_MMC). uint64_t accommodates SD cards larger than 4 GiB.
void fs_health_set_storage_usage(uint64_t used_bytes, uint64_t total_bytes);

// Returns cached stats (always succeeds after fs_health_init()).
void fs_health_get(FSHealthStats* out);

#ifdef __cplusplus
}
#endif
