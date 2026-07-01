#include "storage.h"

#include "fs_health.h"
#include "log_manager.h"

#include <Arduino.h>
#include <esp_partition.h>

#define TAG "Storage"

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

static bool s_mounted = false;

bool storage_mount() {
    if (s_mounted) return true;

#if USE_SD_STORAGE
    // SD card was already mounted in setup() via sd_storage_mount(). `Storage`
    // resolves to SD_MMC and is ready to use.
    LOGI(TAG, "Using SD card storage (mounted earlier in boot)");
    s_mounted = true;
    storage_publish_usage(true);
    if (!Storage.exists("/config")) {
        Storage.mkdir("/config");
    }
    if (!Storage.exists("/storage")) {
        Storage.mkdir("/storage");
    }
#else
    // Find storage partition by subtype (label may vary across boards).
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
        nullptr);
    if (!part) {
        LOGW(TAG, "No storage partition found — persistent config unavailable");
        return false;
    }

    LOGI(TAG, "Found storage partition '%s' (%u KB)", part->label, part->size / 1024);

    if (!Storage.begin(true /* formatOnFail */, "/littlefs", 10, part->label)) {
        LOGE(TAG, "LittleFS mount failed on partition '%s'", part->label);
        return false;
    }

    s_mounted = true;
    storage_publish_usage(true);

    if (!Storage.exists("/config")) {
        Storage.mkdir("/config");
    }

    LOGI(TAG, "LittleFS mounted (total=%u used=%u)", Storage.totalBytes(), Storage.usedBytes());
#endif

    return true;
}
