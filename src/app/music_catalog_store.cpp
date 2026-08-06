#include "music_catalog_store.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

MusicCatalogSnapshot* g_slots = nullptr;
SemaphoreHandle_t g_mutex = nullptr;
StaticSemaphore_t g_mutex_storage;
uint8_t g_active_slot = 0;
bool g_building = false;
MusicCatalogStatus g_status = {false, false, false, 0, 0, MUSIC_CATALOG_UNAVAILABLE};

bool lock_store() {
    return g_mutex && xSemaphoreTake(g_mutex, portMAX_DELAY) == pdTRUE;
}

void unlock_store() {
    xSemaphoreGive(g_mutex);
}

} // namespace

bool music_catalog_store_init() {
    if (g_status.initialized) return true;
    g_mutex = xSemaphoreCreateMutexStatic(&g_mutex_storage);
    if (!g_mutex) return false;
    g_slots = static_cast<MusicCatalogSnapshot*>(heap_caps_calloc(
        2, sizeof(MusicCatalogSnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_slots) return false;
    g_status.initialized = true;
    return true;
}

MusicCatalogSnapshot* music_catalog_store_begin_build() {
    if (!lock_store()) return nullptr;
    MusicCatalogSnapshot* target = nullptr;
    if (g_status.initialized && !g_building) {
        g_building = true;
        target = &g_slots[g_active_slot ^ 1U];
    }
    unlock_store();
    return target;
}

void music_catalog_store_publish(MusicCatalogResult result) {
    if (!lock_store()) return;
    g_building = false;
    g_status.last_refresh_result = result;
    if (result == MUSIC_CATALOG_OK) {
        g_active_slot ^= 1U;
        ++g_status.generation;
        g_status.available = g_slots[g_active_slot].available;
        g_status.count = g_slots[g_active_slot].count;
        g_status.stale = false;
    } else {
        g_status.stale = g_status.available;
    }
    unlock_store();
}

void music_catalog_store_abort(MusicCatalogResult result) {
    music_catalog_store_publish(result == MUSIC_CATALOG_OK ? MUSIC_CATALOG_UNAVAILABLE : result);
}

const MusicCatalogSnapshot* music_catalog_store_active_for_audio() {
    if (!g_status.initialized) return nullptr;
    return &g_slots[g_active_slot];
}

bool music_catalog_store_copy(MusicCatalogSnapshot* out, MusicCatalogStatus* status) {
    if (!out || !lock_store()) return false;
    if (g_status.initialized) *out = g_slots[g_active_slot];
    else *out = {};
    if (status) *status = g_status;
    const bool available = g_status.available;
    unlock_store();
    return available;
}

bool music_catalog_store_status(MusicCatalogStatus* out) {
    if (!out || !lock_store()) return false;
    *out = g_status;
    unlock_store();
    return g_status.initialized;
}