#include "image_library_runtime.h"

#if HAS_IMAGE_LIBRARY

#include "image_library.h"
#include "image_library_config.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

namespace {
ImageLibrarySnapshot* g_snapshot = nullptr;
ImageLibraryCursor g_cursor;
SemaphoreHandle_t g_mutex = nullptr;
uint32_t g_generation = 0;
uint32_t g_last_advance_ms = 0;

ImageLibrarySnapshot* allocate_snapshot() {
    const uint32_t capabilities = psramFound()
        ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        : MALLOC_CAP_8BIT;
    return static_cast<ImageLibrarySnapshot*>(heap_caps_malloc(sizeof(ImageLibrarySnapshot), capabilities));
}

bool copy_path(const char* path, char* out, size_t out_len) {
    if (!out || !out_len) return false;
    out[0] = '\0';
    if (!path) return false;
    strlcpy(out, path, out_len);
    return true;
}

}

void image_library_runtime_init() {
    if (!g_mutex) g_mutex = xSemaphoreCreateMutex();
    if (!g_snapshot) {
        g_snapshot = allocate_snapshot();
        if (!g_snapshot) return;
        memset(g_snapshot, 0, sizeof(*g_snapshot));
    }
    image_library_runtime_reload();
}

bool image_library_runtime_current(char* out, size_t out_len) {
    if (!g_mutex || !g_snapshot || !out || !out_len) return false;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    const bool found = copy_path(g_cursor.current(*g_snapshot), out, out_len);
    xSemaphoreGive(g_mutex);
    return found;
}

bool image_library_runtime_next(char* out, size_t out_len) {
    if (!g_mutex || !g_snapshot || !out || !out_len) return false;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    const bool found = copy_path(g_cursor.next(*g_snapshot), out, out_len);
    if (found) {
        ++g_generation;
        g_last_advance_ms = millis();
    }
    xSemaphoreGive(g_mutex);
    return found;
}

bool image_library_runtime_previous(char* out, size_t out_len) {
    if (!g_mutex || !g_snapshot || !out || !out_len) return false;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    const bool found = copy_path(g_cursor.previous(*g_snapshot), out, out_len);
    if (found) {
        ++g_generation;
        g_last_advance_ms = millis();
    }
    xSemaphoreGive(g_mutex);
    return found;
}

bool image_library_runtime_reload() {
    if (!g_mutex || !g_snapshot) return false;
    ImageLibraryConfig config;
    if (!image_library_config_get(&config)) return false;
    ImageLibrarySnapshot* snapshot = allocate_snapshot();
    if (!snapshot) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    ImageLibraryCatalog catalog;
    const bool discovered = image_library_discover(config.directory, &catalog, snapshot);
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_cursor.configure(config.directory);
    memcpy(g_snapshot, snapshot, sizeof(*g_snapshot));
    g_last_advance_ms = millis();
    ++g_generation;
    xSemaphoreGive(g_mutex);
    free(snapshot);
    return discovered;
}

void image_library_runtime_tick(uint32_t now_ms) {
    if (!g_mutex || !g_snapshot) return;
    ImageLibraryConfig config;
    if (!image_library_config_get(&config)) return;
    const uint32_t interval_ms = static_cast<uint32_t>(config.interval_seconds) * 1000U;
    if (!interval_ms) return;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    if (g_snapshot->count && now_ms - g_last_advance_ms >= interval_ms &&
        g_cursor.next(*g_snapshot)) {
        g_last_advance_ms = now_ms;
        ++g_generation;
    }
    xSemaphoreGive(g_mutex);
}

uint32_t image_library_runtime_generation() {
    if (!g_mutex) return 0;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    const uint32_t generation = g_generation;
    xSemaphoreGive(g_mutex);
    return generation;
}

#endif // HAS_IMAGE_LIBRARY