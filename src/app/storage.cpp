#include "storage.h"

#include "fs_health.h"

#include <Arduino.h>

// Throttle: at most one usage scan per minute. FAT directory traversal on
// large SD cards (used by SDMMCFS::usedBytes) can take hundreds of ms, so
// avoid hammering it from every save path.
static constexpr uint32_t STORAGE_USAGE_MIN_INTERVAL_MS = 60UL * 1000UL;
static uint32_t s_last_publish_ms = 0;
static bool s_have_published = false;

void storage_publish_usage(bool force) {
    const uint32_t now = millis();
    if (!force && s_have_published &&
        (uint32_t)(now - s_last_publish_ms) < STORAGE_USAGE_MIN_INTERVAL_MS) {
        return;
    }
    const uint64_t used = Storage.usedBytes();
    const uint64_t total = Storage.totalBytes();
    fs_health_set_storage_usage(used, total);
    s_last_publish_ms = now;
    s_have_published = true;
}
