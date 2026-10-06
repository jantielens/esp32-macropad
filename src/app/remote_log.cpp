#include "board_config.h"
#if HAS_REMOTE_LOG
#include "remote_log.h"

#include "ota_activity.h"
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <new>
#if CONFIG_IDF_TARGET_ESP32S3
#include <esp_attr.h>
#endif

static_assert(REMOTE_LOG_BUFFER_RECORDS > 0 && REMOTE_LOG_BUFFER_RECORDS < INT32_MAX, "Remote log capacity must be positive and fit sequence arithmetic");
static_assert(REMOTE_LOG_BOOT_RECORDS > 0, "Remote startup capacity must be positive");

namespace {
StaticSemaphore_t capture_mutex_storage;
SemaphoreHandle_t capture_mutex = nullptr;
RemoteLogStore rolling_store;
RemoteLogStore boot_store;
RemoteLogSnapshot* snapshot = nullptr;
std::atomic<bool> available{false};
std::atomic_flag snapshot_busy = ATOMIC_FLAG_INIT;
#if CONFIG_IDF_TARGET_ESP32S3
DRAM_ATTR std::atomic<uint32_t> dropped{0};
static_assert(alignof(decltype(dropped)) >= sizeof(uint32_t), "Remote log counters must be aligned for native S3 atomics");
#else
std::atomic<uint32_t> dropped{0};
static_assert(__atomic_always_lock_free(sizeof(uint32_t), &dropped), "Remote log counters must be lock free");
#endif
std::atomic<bool> boot_complete{false};
bool boot_truncated = false;
bool initialized = false;
uint32_t boot_id = 0;
struct RemoteLogStorage {
    RemoteLogRecord rolling[REMOTE_LOG_BUFFER_RECORDS];
    RemoteLogRecord boot[REMOTE_LOG_BOOT_RECORDS];
    RemoteLogSnapshot response;
};
}

void remote_log_init() {
    if (initialized) return;
    void* memory = heap_caps_malloc(sizeof(RemoteLogStorage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return;
    capture_mutex = xSemaphoreCreateMutexStatic(&capture_mutex_storage);
    if (!capture_mutex) {
        heap_caps_free(memory);
        return;
    }
    auto* storage = new (memory) RemoteLogStorage;
    rolling_store.init(storage->rolling, REMOTE_LOG_BUFFER_RECORDS);
    boot_store.init(storage->boot, REMOTE_LOG_BOOT_RECORDS);
    snapshot = &storage->response;
    boot_id = esp_random();
    initialized = true;
    available.store(true, std::memory_order_release);
}

bool remote_log_available() { return available.load(std::memory_order_acquire); }

void remote_log_append(const char* line) {
    if (!remote_log_available()) return;
    if (ota_activity_is_active() || xSemaphoreTake(capture_mutex, 0) != pdTRUE) {
        dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    rolling_store.append(line);
    if (!boot_complete.load(std::memory_order_relaxed)) {
        if (boot_store.count() < REMOTE_LOG_BOOT_RECORDS) boot_store.append(line);
        else boot_truncated = true;
    }
    xSemaphoreGive(capture_mutex);
}

void remote_log_finish_boot() { boot_complete.store(true, std::memory_order_relaxed); }

const RemoteLogSnapshot* remote_log_snapshot(bool boot, bool has_after, uint32_t after, size_t limit) {
    if (!remote_log_available() || ota_activity_is_active() || snapshot_busy.test_and_set(std::memory_order_acquire)) return nullptr;
    if (xSemaphoreTake(capture_mutex, 0) != pdTRUE) {
        remote_log_release_snapshot();
        return nullptr;
    }
    RemoteLogStore& store = boot ? boot_store : rolling_store;
    snapshot->range = store.range(has_after, after, limit);
    snapshot->boot_id = boot_id;
    snapshot->dropped = dropped.load(std::memory_order_relaxed);
    snapshot->capacity = boot ? REMOTE_LOG_BOOT_RECORDS : REMOTE_LOG_BUFFER_RECORDS;
    snapshot->boot_complete = boot_complete.load(std::memory_order_relaxed);
    snapshot->boot_truncated = boot_truncated;
    snapshot->count = 0;
    const size_t requested = snapshot->range.count;
    for (size_t index = 0; index < requested; ++index) {
        if (ota_activity_is_active()) {
            xSemaphoreGive(capture_mutex);
            remote_log_release_snapshot();
            return nullptr;
        }
        const uint32_t sequence = ++snapshot->range.cursor;
        if (store.get(sequence, snapshot->records[snapshot->count])) ++snapshot->count;
        else ++snapshot->range.missed;
    }
    xSemaphoreGive(capture_mutex);
    return snapshot;
}

void remote_log_release_snapshot() { snapshot_busy.clear(std::memory_order_release); }
#endif