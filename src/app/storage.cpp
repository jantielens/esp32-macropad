#include "storage.h"

#if HAS_FILESYSTEM
#include "fs_health.h"
#include "log_manager.h"
#if USE_SD_STORAGE
#include "sd_storage.h"
#endif

#include <Arduino.h>
#include <esp_partition.h>
#include <string.h>

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

bool storage_write_verified(const char* path, const uint8_t* data, size_t length) {
    if (Storage.exists(path) && !Storage.remove(path)) return false;
    File file = Storage.open(path, "w");
    if (!file) return false;
    const size_t written = file.write(data, length);
    file.close();
    if (written != length) {
        Storage.remove(path);
        return false;
    }
    File verify = Storage.open(path, "r");
    if (!verify || verify.size() != length) {
        if (verify) verify.close();
        Storage.remove(path);
        return false;
    }
    uint8_t chunk[256];
    size_t offset = 0;
    while (offset < length) {
        const size_t wanted = length - offset < sizeof(chunk) ? length - offset : sizeof(chunk);
        const size_t read = verify.readBytes((char*)chunk, wanted);
        if (read != wanted || memcmp(chunk, data + offset, wanted) != 0) {
            verify.close();
            Storage.remove(path);
            return false;
        }
        offset += read;
    }
    verify.close();
    return true;
}

bool storage_write_atomic(const char* path, const uint8_t* data, size_t length) {
    char temporary[112], backup[112];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary) ||
        snprintf(backup, sizeof(backup), "%s.bak", path) >= (int)sizeof(backup)) return false;
    if ((Storage.exists(temporary) && !Storage.remove(temporary)) ||
        (Storage.exists(backup) && !Storage.remove(backup)) ||
        !storage_write_verified(temporary, data, length)) return false;
    const bool had_previous = Storage.exists(path);
    if (had_previous && !Storage.rename(path, backup)) {
        Storage.remove(temporary);
        return false;
    }
    if (!Storage.rename(temporary, path)) {
        if (had_previous && !Storage.rename(backup, path))
            LOGE(TAG, "Failed to restore %s after replacement error", path);
        Storage.remove(temporary);
        return false;
    }
    if (had_previous && !Storage.remove(backup))
        LOGW(TAG, "Replacement succeeded but backup cleanup failed: %s", backup);
    return true;
}

bool storage_mount() {
    if (s_mounted) return true;

#if USE_SD_STORAGE
    if (!sd_storage_is_mounted()) {
        LOGE(TAG, "SD card storage was not mounted");
        return false;
    }
    LOGI(TAG, "Using SD card storage");
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
#else
void storage_publish_usage(bool) {}
bool storage_mount() { return false; }
#endif
