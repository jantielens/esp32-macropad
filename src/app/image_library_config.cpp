#include "image_library_config.h"

#if HAS_IMAGE_LIBRARY

#include "image_library_runtime.h"
#include "storage.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

namespace {
constexpr const char* kConfigPath = "/config/image_library.json";
constexpr uint16_t kDefaultIntervalSeconds = 10;
ImageLibraryConfig g_config;
SemaphoreHandle_t g_config_mutex = nullptr;

void set_defaults(ImageLibraryConfig* config) {
    strlcpy(config->directory, IMAGE_LIBRARY_ROOT, sizeof(config->directory));
    config->interval_seconds = kDefaultIntervalSeconds;
}

bool parse_config(JsonObjectConst source, ImageLibraryConfig* config) {
    set_defaults(config);
    const char* directory = source["directory"] | IMAGE_LIBRARY_ROOT;
    const uint16_t interval = source["interval_seconds"] | kDefaultIntervalSeconds;
    if (!ImageLibraryCatalog::is_directory_path(directory) || interval > 3600) return false;
    strlcpy(config->directory, directory, sizeof(config->directory));
    config->interval_seconds = interval;
    return true;
}
}

void image_library_config_init() {
    if (!g_config_mutex) g_config_mutex = xSemaphoreCreateMutex();
    set_defaults(&g_config);
    File file = Storage.open(kConfigPath, "r");
    if (!file) return;
    JsonDocument doc;
    if (!deserializeJson(doc, file)) parse_config(doc.as<JsonObjectConst>(), &g_config);
    file.close();
}

bool image_library_config_get(ImageLibraryConfig* config) {
    if (!config || !g_config_mutex) return false;
    xSemaphoreTake(g_config_mutex, portMAX_DELAY);
    *config = g_config;
    xSemaphoreGive(g_config_mutex);
    return true;
}

bool image_library_config_save_raw(const uint8_t* json, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len)) return false;
    ImageLibraryConfig candidate;
    if (!parse_config(doc.as<JsonObjectConst>(), &candidate)) return false;
    File file = Storage.open(kConfigPath, "w");
    if (!file) return false;
    const bool saved = serializeJson(doc, file) > 0;
    file.close();
    if (!saved) return false;
    if (!g_config_mutex) return false;
    xSemaphoreTake(g_config_mutex, portMAX_DELAY);
    g_config = candidate;
    xSemaphoreGive(g_config_mutex);
    image_library_runtime_reload();
    return true;
}

#endif // HAS_IMAGE_LIBRARY