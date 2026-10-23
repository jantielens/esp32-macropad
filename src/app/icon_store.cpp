#include "icon_store.h"

#if HAS_DISPLAY

#include "log_manager.h"
#include "pad_config.h"
#include "pad_validate.h"

#include "storage.h"
#include "psram_json_allocator.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <string.h>

// LVGL's built-in lodepng for PNG decoding
#define LODEPNG_NO_COMPILE_CPP
#include <libs/lodepng/lodepng.h>

#define TAG "IconStore"

// ============================================================================
// Cache — growable list in PSRAM
// ============================================================================

struct IconEntry {
    char id[CONFIG_ICON_KEY_MAX_LEN];
    char source_id[CONFIG_ICON_SOURCE_ID_MAX_LEN];
    lv_draw_buf_t* draw_buf;
    IconKind kind;
};

static IconEntry* g_entries = nullptr;
static uint16_t g_count = 0;
static uint16_t g_capacity = 0;

struct RetiredIconBuffer {
    uint8_t page;
    lv_draw_buf_t* draw_buf;
};

static RetiredIconBuffer* g_retired = nullptr;
static uint16_t g_retired_count = 0;
static uint16_t g_retired_capacity = 0;

// Guards cache and retired-buffer state against concurrent access by the LVGL
// render task and the async web server task. Created in icon_store_init().
// Critical sections cover only in-memory state — never PNG decode or file I/O.
static SemaphoreHandle_t g_cache_mutex = nullptr;
static SemaphoreHandle_t g_file_mutex = nullptr;

static inline void cache_lock() {
    if (g_cache_mutex) xSemaphoreTake(g_cache_mutex, portMAX_DELAY);
}

static inline void cache_unlock() {
    if (g_cache_mutex) xSemaphoreGive(g_cache_mutex);
}

// ============================================================================
// Internal helpers
// ============================================================================

static bool ensure_capacity() {
    if (g_count < g_capacity) return true;

    uint16_t new_cap = g_capacity == 0 ? 16 : g_capacity * 2;
    size_t new_size = new_cap * sizeof(IconEntry);

    IconEntry* new_entries = nullptr;
    if (psramFound()) {
        new_entries = (IconEntry*)heap_caps_realloc(
            g_entries, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!new_entries) {
        new_entries = (IconEntry*)realloc(g_entries, new_size);
    }
    if (!new_entries) {
        LOGE(TAG, "Failed to grow cache to %u entries", new_cap);
        return false;
    }

    g_entries = new_entries;
    g_capacity = new_cap;
    return true;
}

static bool ensure_retired_capacity() {
    if (g_retired_count < g_retired_capacity) return true;

    uint16_t new_capacity = g_retired_capacity == 0 ? 16 : g_retired_capacity * 2;
    size_t new_size = (size_t)new_capacity * sizeof(RetiredIconBuffer);
    RetiredIconBuffer* new_retired = nullptr;
    if (psramFound()) {
        new_retired = (RetiredIconBuffer*)heap_caps_realloc(
            g_retired, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!new_retired) {
        new_retired = (RetiredIconBuffer*)realloc(g_retired, new_size);
    }
    if (!new_retired) {
        LOGE(TAG, "Failed to grow retired cache to %u buffers", new_capacity);
        return false;
    }

    g_retired = new_retired;
    g_retired_capacity = new_capacity;
    return true;
}

static uint8_t page_from_icon_id(const char* id) {
    unsigned page = 0;
    if (!id || sscanf(id, "pad_%u_", &page) != 1 || page >= MAX_PADS) {
        return UINT8_MAX;
    }
    return (uint8_t)page;
}

static bool retire_buffer(uint8_t page, lv_draw_buf_t* draw_buf) {
    if (!draw_buf) return true;
    if (!ensure_retired_capacity()) return false;

    g_retired[g_retired_count++] = {page, draw_buf};
    return true;
}

static int find_entry(const char* id) {
    for (uint16_t i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].id, id) == 0) return (int)i;
    }
    return -1;
}

static bool cache_entry_matches(const char* id, const char* source_id, IconKind kind) {
    cache_lock();
    const int index = find_entry(id);
    const bool matches = index >= 0 && g_entries[index].kind == kind &&
                         strcmp(g_entries[index].source_id, source_id) == 0;
    cache_unlock();
    return matches;
}

static bool invalidate_cache_entry(const char* id, uint8_t page) {
    cache_lock();
    const int index = find_entry(id);
    if (index < 0) {
        cache_unlock();
        return true;
    }
    if (!retire_buffer(page, g_entries[index].draw_buf)) {
        cache_unlock();
        return false;
    }
    if (index != g_count - 1) g_entries[index] = g_entries[g_count - 1];
    g_count--;
    cache_unlock();
    return true;
}

// Remove cache entries from the lookup table and retire their buffers until
// the corresponding LVGL pad screen deletes its image widgets.
static uint16_t invalidate_entries_with_prefix(const char* prefix, uint8_t page) {
    if (!prefix || !prefix[0]) return 0;

    const size_t prefix_len = strlen(prefix);
    uint16_t removed = 0;

    cache_lock();
    for (uint16_t i = 0; i < g_count; ) {
        if (strncmp(g_entries[i].id, prefix, prefix_len) == 0) {
            if (!retire_buffer(page, g_entries[i].draw_buf)) {
                break;
            }
            if (i != g_count - 1) {
                g_entries[i] = g_entries[g_count - 1];
            }
            g_count--;
            removed++;
            continue;
        }
        i++;
    }
    cache_unlock();

    return removed;
}

static bool validate_png(const uint8_t* data, size_t len) {
    // PNG signature: 89 50 4E 47 0D 0A 1A 0A
    if (len < 8) return false;
    return (data[0] == 0x89 && data[1] == 0x50 &&
            data[2] == 0x4E && data[3] == 0x47);
}

static uint32_t read_be32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static bool png_dimensions_allowed(const uint8_t* data, size_t len,
                                   unsigned* width, unsigned* height) {
    if (!validate_png(data, len) || len < 24 ||
        read_be32(data + 8) != 13 || memcmp(data + 12, "IHDR", 4) != 0) {
        return false;
    }

    const uint32_t png_width = read_be32(data + 16);
    const uint32_t png_height = read_be32(data + 20);
    if (width) *width = png_width;
    if (height) *height = png_height;
    return png_width > 0 && png_height > 0 &&
           png_width <= ICON_MAX_DIMENSION && png_height <= ICON_MAX_DIMENSION;
}

static bool source_matches_metadata_unlocked(const char* source_id,
                                            const char* catalog_id,
                                            const char* kind) {
    if (!pad_icon_source_id_valid(source_id)) return false;
    char path[64];
    snprintf(path, sizeof(path), "/icon_sources/%s.json", source_id);
    File file = Storage.open(path, "r");
    if (!file || file.size() > ICON_MAX_SOURCE_JSON_SIZE) {
        if (file) file.close();
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> filter(128);
    filter["catalog_id"] = true;
    filter["kind"] = true;
    BasicJsonDocument<PsramJsonAllocator> document(256);
    const DeserializationError error = deserializeJson(
        document, file, DeserializationOption::Filter(filter));
    file.close();
    if (error || document.overflowed()) return false;
    const JsonObjectConst source = document.as<JsonObjectConst>();
    const char* stored_catalog_id = source["catalog_id"] | "";
    const char* source_kind = source["kind"] | "";
    return pad_icon_catalog_kind_valid(stored_catalog_id, source_kind) &&
        (!catalog_id || strcmp(catalog_id, stored_catalog_id) == 0) &&
        strcmp(kind, source_kind) == 0;
}

static bool source_matches_kind_unlocked(const char* source_id, IconKind kind) {
    return source_matches_metadata_unlocked(source_id, nullptr,
        kind == ICON_KIND_MONO ? "mono" : "color");
}

static bool source_reference_scan(const char* source_id, bool* referenced) {
    *referenced = false;
    for (uint8_t page = 0; page < MAX_PADS; ++page) {
        if (!pad_config_exists(page)) continue;
        const PadConfig* config = pad_config_acquire(page);
        if (!config) return false;
        for (uint8_t index = 0; index < config->button_count; ++index) {
            if (strcmp(config->buttons[index].icon_source_id, source_id) == 0) {
                *referenced = true;
                break;
            }
        }
        pad_config_release(config);
        if (*referenced) return true;
    }
    return true;
}

static bool read_artifact_unlocked(const char* key, IconArtifactInfo* out) {
    char png_path[64];
    char metadata_path[72];
    snprintf(png_path, sizeof(png_path), "/icons/%s.png", key);
    snprintf(metadata_path, sizeof(metadata_path), "/icons/%s.meta", key);
    File metadata_file = Storage.open(metadata_path, "r");
    if (!metadata_file || metadata_file.size() > 256) {
        if (metadata_file) metadata_file.close();
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> metadata(512);
    const DeserializationError error = deserializeJson(metadata, metadata_file);
    metadata_file.close();
    if (error || metadata.overflowed()) return false;
    const char* stored_source = metadata["source_id"] | "";
    const uint8_t stored_kind = metadata["kind"] | (uint8_t)255;
    const uint32_t stored_width = metadata["width"] | (uint32_t)0;
    const uint32_t stored_height = metadata["height"] | (uint32_t)0;
    if (!pad_icon_source_id_valid(stored_source) || stored_kind > ICON_KIND_MONO ||
        !stored_width || !stored_height || stored_width > ICON_MAX_DIMENSION ||
        stored_height > ICON_MAX_DIMENSION) return false;

    File png_file = Storage.open(png_path, "r");
    if (!png_file || png_file.size() < 24 || png_file.size() > ICON_MAX_PNG_SIZE) {
        if (png_file) png_file.close();
        return false;
    }
    uint8_t header[24];
    const size_t read = png_file.readBytes((char*)header, sizeof(header));
    png_file.close();
    unsigned width = 0, height = 0;
    if (read != sizeof(header) ||
        !png_dimensions_allowed(header, sizeof(header), &width, &height) ||
        width != stored_width || height != stored_height) return false;
    strlcpy(out->source_id, stored_source, sizeof(out->source_id));
    out->kind = (IconKind)stored_kind;
    out->width = stored_width;
    out->height = stored_height;
    out->render_key = metadata["render_key"] | (uint32_t)0;
    return true;
}

static bool artifact_matches_unlocked(const char* key, const char* source_id,
                                      IconKind kind, uint32_t render_key = 0) {
    IconArtifactInfo info = {};
    return read_artifact_unlocked(key, &info) &&
        strcmp(info.source_id, source_id) == 0 && info.kind == kind &&
        (!render_key || info.render_key == render_key);
}

    static bool artifact_matches_for_preload(const char* key, const char* source_id,
                         IconKind kind) {
        if (!g_file_mutex) return false;
        xSemaphoreTake(g_file_mutex, portMAX_DELAY);
        const bool matches = source_matches_kind_unlocked(source_id, kind) &&
                 artifact_matches_unlocked(key, source_id, kind);
        xSemaphoreGive(g_file_mutex);
        return matches;
    }

static bool recover_icon_directory_entry(const char* directory) {
    File dir = Storage.open(directory);
    if (!dir || !dir.isDirectory()) return false;
    File entry = dir.openNextFile();
    while (entry) {
        char name[64] = {};
        const char* raw_name = entry.name();
        const char* basename = raw_name ? strrchr(raw_name, '/') : nullptr;
        strlcpy(name, basename ? basename + 1 : (raw_name ? raw_name : ""), sizeof(name));
        const bool is_directory = entry.isDirectory();
        entry.close();
        if (!is_directory) {
            const size_t length = strlen(name);
            const bool temporary = length > 4 && strcmp(name + length - 4, ".tmp") == 0;
            const bool backup = length > 4 && strcmp(name + length - 4, ".bak") == 0;
            if (temporary || backup) {
                char path[112];
                snprintf(path, sizeof(path), "%s/%s", directory, name);
                dir.close();
                bool recovered;
                if (temporary) {
                    recovered = Storage.remove(path);
                } else {
                    name[length - 4] = '\0';
                    char target[112];
                    snprintf(target, sizeof(target), "%s/%s", directory, name);
                    recovered = Storage.exists(target) ? Storage.remove(path) : Storage.rename(path, target);
                }
                if (!recovered) LOGW(TAG, "Could not recover icon file %s", path);
                return recovered;
            }
        }
        entry = dir.openNextFile();
    }
    dir.close();
    return false;
}

static bool write_icon_pair(const char* png_path, const char* metadata_path,
                            const uint8_t* png_data, size_t png_length,
                            const uint8_t* metadata, size_t metadata_length) {
    char png_tmp[112], metadata_tmp[112], png_backup[112], metadata_backup[112];
    if (snprintf(png_tmp, sizeof(png_tmp), "%s.tmp", png_path) >= (int)sizeof(png_tmp) ||
        snprintf(metadata_tmp, sizeof(metadata_tmp), "%s.tmp", metadata_path) >= (int)sizeof(metadata_tmp) ||
        snprintf(png_backup, sizeof(png_backup), "%s.bak", png_path) >= (int)sizeof(png_backup) ||
        snprintf(metadata_backup, sizeof(metadata_backup), "%s.bak", metadata_path) >= (int)sizeof(metadata_backup)) return false;
    if ((Storage.exists(png_tmp) && !Storage.remove(png_tmp)) ||
        (Storage.exists(metadata_tmp) && !Storage.remove(metadata_tmp)) ||
        (Storage.exists(png_backup) && !Storage.remove(png_backup)) ||
        (Storage.exists(metadata_backup) && !Storage.remove(metadata_backup)) ||
        !storage_write_verified(png_tmp, png_data, png_length) ||
        !storage_write_verified(metadata_tmp, metadata, metadata_length)) {
        Storage.remove(png_tmp);
        Storage.remove(metadata_tmp);
        return false;
    }

    const bool had_png = Storage.exists(png_path);
    const bool had_metadata = Storage.exists(metadata_path);
    if ((had_png && !Storage.rename(png_path, png_backup)) ||
        (had_metadata && !Storage.rename(metadata_path, metadata_backup))) {
        if (had_png && Storage.exists(png_backup)) Storage.rename(png_backup, png_path);
        Storage.remove(png_tmp);
        Storage.remove(metadata_tmp);
        return false;
    }
    if (!Storage.rename(png_tmp, png_path) || !Storage.rename(metadata_tmp, metadata_path)) {
        Storage.remove(png_path);
        Storage.remove(metadata_path);
        const bool restored_png = !had_png || Storage.rename(png_backup, png_path);
        const bool restored_metadata = !had_metadata || Storage.rename(metadata_backup, metadata_path);
        if (!restored_png || !restored_metadata)
            LOGE(TAG, "Failed to restore generated icon pair %s", png_path);
        Storage.remove(png_tmp);
        Storage.remove(metadata_tmp);
        return false;
    }
    if (had_png) Storage.remove(png_backup);
    if (had_metadata) Storage.remove(metadata_backup);
    return true;
}

// lodepng outputs RGBA byte order; LVGL ARGB8888 expects BGRA. Swap R↔B.
static void swap_rb(lv_draw_buf_t* buf) {
    uint8_t* data = buf->data;
    uint32_t stride = buf->header.stride;
    uint32_t h = buf->header.h;
    uint32_t w = buf->header.w;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t* row = data + y * stride;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t tmp = row[x * 4 + 0];      // R
            row[x * 4 + 0] = row[x * 4 + 2];   // R ← B
            row[x * 4 + 2] = tmp;               // B ← R
        }
    }
}

// Decode PNG data into an LVGL draw buffer with R↔B swap applied.
// Returns nullptr on failure. Caller takes ownership of the returned buffer.
static lv_draw_buf_t* decode_png(const uint8_t* data, size_t len, const char* label) {
    unsigned char* out = nullptr;
    unsigned w = 0, h = 0;
    unsigned err = lodepng_decode32(&out, &w, &h, data, len);

    if (err || !out) {
        LOGW(TAG, "PNG decode failed for '%s': %s", label,
             err ? lodepng_error_text(err) : "null output");
        if (out) lv_draw_buf_destroy((lv_draw_buf_t*)out);
        return nullptr;
    }

    lv_draw_buf_t* draw_buf = (lv_draw_buf_t*)out;

    if (w == 0 || h == 0 || w > ICON_MAX_DIMENSION || h > ICON_MAX_DIMENSION) {
        LOGW(TAG, "PNG dims invalid for '%s': %ux%u", label, w, h);
        lv_draw_buf_destroy(draw_buf);
        return nullptr;
    }

    swap_rb(draw_buf);
    return draw_buf;
}

// Add or update a cache entry. Takes ownership of the lv_draw_buf_t.
static bool cache_entry_buf(const char* id, const char* source_id,
                            IconKind kind, lv_draw_buf_t* buf) {
    cache_lock();

    // Check if entry already exists (update in place)
    int idx = find_entry(id);
    if (idx >= 0) {
        IconEntry& e = g_entries[idx];
        if (!retire_buffer(page_from_icon_id(id), e.draw_buf)) {
            cache_unlock();
            lv_draw_buf_destroy(buf);
            return false;
        }
        e.draw_buf = buf;
        e.kind = kind;
        strlcpy(e.source_id, source_id ? source_id : "", sizeof(e.source_id));
        cache_unlock();
        LOGT(TAG, "Updated cache: '%s' %ux%u stride=%u", id,
             buf->header.w, buf->header.h, (unsigned)buf->header.stride);
        return true;
    }

    // New entry
    if (!ensure_capacity()) {
        cache_unlock();
        lv_draw_buf_destroy(buf);
        return false;
    }

    IconEntry& e = g_entries[g_count];
    strlcpy(e.id, id, CONFIG_ICON_KEY_MAX_LEN);
    strlcpy(e.source_id, source_id ? source_id : "", sizeof(e.source_id));
    e.draw_buf = buf;
    e.kind = kind;

    g_count++;
    uint16_t total = g_count;
    cache_unlock();
    LOGT(TAG, "Cached: '%s' %ux%u stride=%u kind=%u (%u total)",
         id, buf->header.w, buf->header.h,
         (unsigned)buf->header.stride, kind, total);
    return true;
}

// Load an icon PNG from LittleFS, decode, and cache.
// If cache_key is provided, the icon is cached under that key instead of fs_id.
static bool load_from_fs(const char* fs_id, IconKind kind, const char* source_id,
                         const char* cache_key = nullptr) {
    char path[64];
    snprintf(path, sizeof(path), "/icons/%s.png", fs_id);

    if (!Storage.exists(path)) return false;

    File f = Storage.open(path, "r");
    if (!f) return false;

    size_t file_size = f.size();
    if (file_size < 8 || file_size > ICON_MAX_PNG_SIZE) {
        f.close();
        LOGW(TAG, "Invalid file size for '%s': %u", fs_id, (unsigned)file_size);
        return false;
    }

    uint8_t* buf = nullptr;
    if (psramFound()) {
        buf = (uint8_t*)heap_caps_malloc(file_size,
                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!buf) buf = (uint8_t*)malloc(file_size);
    if (!buf) {
        f.close();
        LOGE(TAG, "OOM reading '%s' (%u bytes)", fs_id, (unsigned)file_size);
        return false;
    }

    size_t read = f.readBytes((char*)buf, file_size);
    f.close();

    if (read != file_size) {
        free(buf);
        LOGW(TAG, "Short read for '%s': %u/%u", fs_id, (unsigned)read, (unsigned)file_size);
        return false;
    }

    unsigned width = 0;
    unsigned height = 0;
    if (!png_dimensions_allowed(buf, file_size, &width, &height)) {
        free(buf);
        LOGW(TAG, "PNG rejected for '%s': %ux%u exceeds %u px limit or has invalid IHDR",
             fs_id, width, height, ICON_MAX_DIMENSION);
        return false;
    }

    // Decode PNG → LVGL draw buffer
    lv_draw_buf_t* draw_buf = decode_png(buf, file_size, fs_id);
    free(buf);

    if (!draw_buf) return false;

    const char* key = cache_key ? cache_key : fs_id;
    LOGT(TAG, "Decoded '%s': %ux%u stride=%u cf=%u%s",
         fs_id, draw_buf->header.w, draw_buf->header.h,
         draw_buf->header.stride, draw_buf->header.cf,
         cache_key ? " (alias)" : "");

    return cache_entry_buf(key, source_id, kind, draw_buf);
}

// ============================================================================
// Public API
// ============================================================================

void icon_store_build_key(uint8_t page, const char* layer, uint8_t col, uint8_t row,
                          char* out, size_t out_size) {
    snprintf(out, out_size, "pad_%u_%s_%u_%u", page,
             layer && strcmp(layer, "overlay") == 0 ? "overlay" : "main", col, row);
}

void icon_store_init() {
    if (!g_cache_mutex) {
        g_cache_mutex = xSemaphoreCreateMutex();
        if (!g_cache_mutex) {
            LOGE(TAG, "Failed to create cache mutex");
        }
    }
    if (!g_file_mutex) {
        g_file_mutex = xSemaphoreCreateMutex();
        if (!g_file_mutex) LOGE(TAG, "Failed to create file mutex");
    }

    if (!Storage.exists("/icons")) {
        Storage.mkdir("/icons");
        LOGI(TAG, "Created /icons directory");
    }
    if (!Storage.exists("/icon_sources")) {
        Storage.mkdir("/icon_sources");
        LOGI(TAG, "Created /icon_sources directory");
    }
    while (recover_icon_directory_entry("/icons")) {}
    while (recover_icon_directory_entry("/icon_sources")) {}
    LOGI(TAG, "Initialized");
}

bool icon_store_source_id_valid(const char* source_id) {
    return pad_icon_source_id_valid(source_id);
}

bool icon_store_key_valid(const char* key) {
    if (!key || !key[0] || strlen(key) >= CONFIG_ICON_KEY_MAX_LEN) return false;
    unsigned page = 0, col = 0, row = 0;
    char layer[9] = {};
    char trailing = '\0';
    if (sscanf(key, "pad_%u_%8[^_]_%u_%u%c", &page, layer, &col, &row, &trailing) != 4 ||
        page >= MAX_PADS || col >= MAX_GRID_COLS || row >= MAX_GRID_ROWS ||
        (strcmp(layer, "main") != 0 && strcmp(layer, "overlay") != 0)) return false;
    char canonical[CONFIG_ICON_KEY_MAX_LEN];
    icon_store_build_key((uint8_t)page, layer, (uint8_t)col, (uint8_t)row,
                         canonical, sizeof(canonical));
    return strcmp(key, canonical) == 0;
}

bool icon_store_source_exists(const char* source_id) {
    if (!pad_icon_source_id_valid(source_id)) return false;
    char path[64];
    snprintf(path, sizeof(path), "/icon_sources/%s.json", source_id);
    return Storage.exists(path);
}

bool icon_store_source_matches_kind(const char* source_id, IconKind kind) {
    if (!g_file_mutex || xSemaphoreTake(g_file_mutex, 0) != pdTRUE) return false;
    const bool matches = source_matches_kind_unlocked(source_id, kind);
    xSemaphoreGive(g_file_mutex);
    return matches;
}

bool icon_store_source_matches_metadata(const char* source_id,
                                       const char* catalog_id,
                                       const char* kind) {
    if (!catalog_id || !kind || !g_file_mutex ||
        xSemaphoreTake(g_file_mutex, 0) != pdTRUE) return false;
    const bool matches = source_matches_metadata_unlocked(source_id, catalog_id, kind);
    xSemaphoreGive(g_file_mutex);
    return matches;
}

bool icon_store_artifact_matches(const char* key, const char* source_id, IconKind kind,
                                 uint32_t render_key) {
    if (!icon_store_key_valid(key) || !pad_icon_source_id_valid(source_id) ||
        !g_file_mutex || xSemaphoreTake(g_file_mutex, 0) != pdTRUE) return false;
    const bool matches = source_matches_kind_unlocked(source_id, kind) &&
        artifact_matches_unlocked(key, source_id, kind, render_key);
    xSemaphoreGive(g_file_mutex);
    return matches;
}

bool icon_store_read_artifact(const char* key, IconArtifactInfo* out) {
    if (!out || !icon_store_key_valid(key) || !g_file_mutex ||
        xSemaphoreTake(g_file_mutex, 0) != pdTRUE) return false;
    const bool valid = read_artifact_unlocked(key, out);
    xSemaphoreGive(g_file_mutex);
    return valid;
}

bool icon_store_source_delete_if_unreferenced(const char* source_id) {
    if (!pad_icon_source_id_valid(source_id) || !g_file_mutex ||
        xSemaphoreTake(g_file_mutex, 0) != pdTRUE) return false;
    bool referenced = false;
    const bool scanned = source_reference_scan(source_id, &referenced);
    bool removed = false;
    if (scanned && !referenced) {
        char path[64];
        snprintf(path, sizeof(path), "/icon_sources/%s.json", source_id);
        removed = !Storage.exists(path) || Storage.remove(path);
    }
    xSemaphoreGive(g_file_mutex);
    return scanned && removed;
}

void icon_store_cleanup_unreferenced_sources() {
    for (uint8_t page = 0; page < MAX_PADS; ++page) {
        if (!pad_config_exists(page)) continue;
        const PadConfig* config = pad_config_acquire(page);
        if (!config) {
            LOGW(TAG, "Skipping source cleanup: pad %u config could not be read", page);
            return;
        }
        pad_config_release(config);
    }

    for (;;) {
        File dir = Storage.open("/icon_sources");
        if (!dir || !dir.isDirectory()) return;
        bool removed = false;
        File entry = dir.openNextFile();
        while (entry) {
            char name[48] = {};
            const char* raw_name = entry.name();
            const char* basename = raw_name ? strrchr(raw_name, '/') : nullptr;
            strlcpy(name, basename ? basename + 1 : (raw_name ? raw_name : ""), sizeof(name));
            const bool is_directory = entry.isDirectory();
            entry.close();
            const size_t length = strlen(name);
            if (!is_directory && length == 31 && strcmp(name + 26, ".json") == 0) {
                name[26] = '\0';
                if (pad_icon_source_id_valid(name) && icon_store_source_delete_if_unreferenced(name)) {
                    removed = true;
                    break;
                }
            }
            entry = dir.openNextFile();
        }
        dir.close();
        if (!removed) break;
    }
}

IconStoreResult icon_store_source_install(const uint8_t* json, size_t json_len,
                                         char* source_id, size_t source_id_len) {
    if (!json || !json_len || !source_id || source_id_len < CONFIG_ICON_SOURCE_ID_MAX_LEN)
        return ICON_STORE_INVALID;
    if (json_len > ICON_MAX_SOURCE_JSON_SIZE) return ICON_STORE_TOO_LARGE;
    if (!g_file_mutex || xSemaphoreTake(g_file_mutex, 0) != pdTRUE)
        return ICON_STORE_BUSY;

    BasicJsonDocument<PsramJsonAllocator> document(json_len * 2 + 512);
    if (!document.capacity()) {
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_UNAVAILABLE;
    }
    const DeserializationError error = deserializeJson(document, json, json_len);
    if (error || document.overflowed()) {
        xSemaphoreGive(g_file_mutex);
        return document.overflowed() ? ICON_STORE_UNAVAILABLE : ICON_STORE_INVALID;
    }
    const JsonObjectConst source = document.as<JsonObjectConst>();
    if (source.isNull() || source.size() != 4) {
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_INVALID;
    }
    const char* catalog_id = source["catalog_id"] | "";
    const char* kind = source["kind"] | "";
    const char* media_type = source["media_type"] | "";
    const char* data = source["data"] | "";
    if (!pad_icon_catalog_kind_valid(catalog_id, kind) ||
        strcmp(media_type, "image/svg+xml") != 0 || !data[0]) {
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_INVALID;
    }

    char id[CONFIG_ICON_SOURCE_ID_MAX_LEN] = {};
    char path[64];
    bool generated = false;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        snprintf(id, sizeof(id), "s_%08x%08x%08x",
                 (unsigned)esp_random(), (unsigned)esp_random(), (unsigned)esp_random());
        snprintf(path, sizeof(path), "/icon_sources/%s.json", id);
        if (!Storage.exists(path)) {
            generated = true;
            break;
        }
    }
    if (!generated) {
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_CONFLICT;
    }
    if (!storage_write_atomic(path, json, json_len)) {
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_IO_ERROR;
    }
    strlcpy(source_id, id, source_id_len);
    xSemaphoreGive(g_file_mutex);
    return ICON_STORE_OK;
}

IconStoreResult icon_store_install(const char* key, IconKind kind,
                                   const char* source_id, uint32_t render_key,
                                   const uint8_t* png_data, size_t png_len) {
    if (!icon_store_key_valid(key) || !pad_icon_source_id_valid(source_id) ||
        !render_key || !png_data || png_len < 8) return ICON_STORE_INVALID;
    if (png_len > ICON_MAX_PNG_SIZE) return ICON_STORE_TOO_LARGE;
    if (!g_file_mutex || xSemaphoreTake(g_file_mutex, 0) != pdTRUE)
        return ICON_STORE_BUSY;
    if (!source_matches_kind_unlocked(source_id, kind)) {
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_INVALID;
    }

    unsigned width = 0;
    unsigned height = 0;
    if (!png_dimensions_allowed(png_data, png_len, &width, &height)) {
        LOGW(TAG, "Install rejected for '%s': %ux%u exceeds %u px limit or has invalid IHDR",
             key, width, height, ICON_MAX_DIMENSION);
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_INVALID;
    }

    lv_draw_buf_t* draw_buf = decode_png(png_data, png_len, key);
    if (!draw_buf) {
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_INVALID;
    }

    char png_path[64];
    char metadata_path[72];
    char metadata[160];
    snprintf(png_path, sizeof(png_path), "/icons/%s.png", key);
    snprintf(metadata_path, sizeof(metadata_path), "/icons/%s.meta", key);
    const int metadata_length = snprintf(metadata, sizeof(metadata),
        "{\"source_id\":\"%s\",\"kind\":%u,\"width\":%u,\"height\":%u,\"render_key\":%u}",
        source_id, (unsigned)kind, width, height, (unsigned)render_key);
    if (metadata_length <= 0 || metadata_length >= (int)sizeof(metadata) ||
        !write_icon_pair(png_path, metadata_path, png_data, png_len,
                         (const uint8_t*)metadata, (size_t)metadata_length)) {
        lv_draw_buf_destroy(draw_buf);
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_IO_ERROR;
    }

    if (!cache_entry_buf(key, source_id, kind, draw_buf)) {
        xSemaphoreGive(g_file_mutex);
        LOGW(TAG, "Cache failed for '%s'; generated icon is saved to disk", key);
        return ICON_STORE_OK;
    }
    xSemaphoreGive(g_file_mutex);
    LOGI(TAG, "Installed: '%s' %ux%u (PNG %u bytes)",
         key, width, height, (unsigned)png_len);
    return ICON_STORE_OK;
}

bool icon_store_lookup(const char* id, IconRef* out) {
    if (!id || !id[0] || !out) return false;

    cache_lock();
    int idx = find_entry(id);
    if (idx < 0) {
        cache_unlock();
        return false;
    }

    const lv_draw_buf_t* buf = g_entries[idx].draw_buf;
    out->dsc = (const lv_image_dsc_t*)buf;
    out->kind = g_entries[idx].kind;
    cache_unlock();
    return true;
}

// Preload icons for a single pad (with template fallback).
static uint16_t preload_pad_icons(uint8_t page, const PadConfig* cfg) {
    uint16_t loaded = 0;
    for (uint8_t i = 0; i < cfg->button_count; i++) {
        if (!cfg->buttons[i].icon_source_id[0]) continue;

        char key[CONFIG_ICON_KEY_MAX_LEN];
        const char* layer = i >= cfg->main_button_count ? "overlay" : "main";
        icon_store_build_key(page, layer, cfg->buttons[i].col,
                             cfg->buttons[i].row, key, sizeof(key));

        IconKind kind = (IconKind)cfg->buttons[i].icon_kind;
        if (cache_entry_matches(key, cfg->buttons[i].icon_source_id, kind)) continue;
        if (!invalidate_cache_entry(key, page)) continue;
        if (artifact_matches_for_preload(key, cfg->buttons[i].icon_source_id, kind)) {
            if (load_from_fs(key, kind, cfg->buttons[i].icon_source_id)) loaded++;
            continue;
        }

        // Fallback: if this pad uses a template, try loading icon from
        // the template pad's position (icon files live under original key)
        if (cfg->template_pad >= 0 && cfg->template_pad != (int8_t)page) {
            char tpl_key[CONFIG_ICON_KEY_MAX_LEN];
            icon_store_build_key((uint8_t)cfg->template_pad, "main",
                                 cfg->buttons[i].col,
                                 cfg->buttons[i].row,
                                 tpl_key, sizeof(tpl_key));
            if (artifact_matches_for_preload(tpl_key, cfg->buttons[i].icon_source_id, kind) &&
                load_from_fs(tpl_key, kind, cfg->buttons[i].icon_source_id, key)) loaded++;
        }
    }
    return loaded;
}

void icon_store_preload_pad_pages() {
    uint16_t loaded = 0;

    for (uint8_t page = 0; page < MAX_PADS; page++) {
        const PadConfig* cfg = pad_config_acquire(page);
        if (!cfg) continue;

        loaded += preload_pad_icons(page, cfg);
        pad_config_release(cfg);
    }

    icon_store_cleanup_unreferenced_sources();

    LOGI(TAG, "Preload complete: %u icons loaded, %u total cached",
         loaded, g_count);
}

void icon_store_preload_pad(uint8_t page) {
    if (page >= MAX_PADS) return;

    const PadConfig* cfg = pad_config_acquire(page);
    if (!cfg) return;

    uint16_t loaded = preload_pad_icons(page, cfg);
    pad_config_release(cfg);

    if (loaded > 0) {
        LOGI(TAG, "Preload pad %u: %u icons loaded", page, loaded);
    }
}

void icon_store_delete_page_icons(uint8_t page) {
    if (page >= MAX_PADS) return;

    char prefix[16];
    snprintf(prefix, sizeof(prefix), "pad_%u_", page);
    size_t prefix_len = strlen(prefix);

    File dir = Storage.open("/icons");
    if (!dir || !dir.isDirectory()) return;

    // Collect filenames first (can't delete while iterating)
    char to_delete[MAX_PAD_BUTTONS * 2][48];
    uint16_t del_count = 0;

    File entry = dir.openNextFile();
    while (entry && del_count < MAX_PAD_BUTTONS * 2) {
        const char* name = entry.name();
        if (name && strncmp(name, prefix, prefix_len) == 0) {
            strlcpy(to_delete[del_count], name, sizeof(to_delete[0]));
            del_count++;
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    for (uint8_t i = 0; i < del_count; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/icons/%s", to_delete[i]);
        Storage.remove(path);
    }

    uint16_t invalidated = invalidate_entries_with_prefix(prefix, page);

    if (del_count > 0 || invalidated > 0) {
        LOGI(TAG, "Deleted %u icon files and invalidated %u cache entries for page %u",
             del_count, invalidated, page);
    }
}

bool icon_store_delete_file(const char* filename) {
    if (!filename) return false;
    const size_t filename_len = strlen(filename);
    if (filename_len <= 4 || strcmp(filename + filename_len - 4, ".png") != 0 ||
        filename_len - 4 >= CONFIG_ICON_KEY_MAX_LEN) {
        return false;
    }

    char key[CONFIG_ICON_KEY_MAX_LEN];
    memcpy(key, filename, filename_len - 4);
    key[filename_len - 4] = '\0';
    if (!icon_store_key_valid(key)) return false;

    char path[64];
    snprintf(path, sizeof(path), "/icons/%s", filename);
    if (!Storage.exists(path) || !Storage.remove(path)) return false;

    char metadata_path[72];
    snprintf(metadata_path, sizeof(metadata_path), "/icons/%s.meta", key);
    Storage.remove(metadata_path);

    const uint8_t page = page_from_icon_id(key);
    cache_lock();
    const int index = find_entry(key);
    if (index >= 0 && retire_buffer(page, g_entries[index].draw_buf)) {
        if (index != g_count - 1) g_entries[index] = g_entries[g_count - 1];
        g_count--;
    }
    cache_unlock();
    return true;
}

void icon_store_collect_retired(uint8_t page) {
    cache_lock();
    for (uint16_t i = 0; i < g_retired_count; ) {
        if (g_retired[i].page != page) {
            i++;
            continue;
        }

        lv_draw_buf_destroy(g_retired[i].draw_buf);
        if (i != g_retired_count - 1) {
            g_retired[i] = g_retired[g_retired_count - 1];
        }
        g_retired_count--;
    }
    cache_unlock();
}

uint16_t icon_store_cache_count() {
    cache_lock();
    uint16_t count = g_count;
    cache_unlock();
    return count;
}

void icon_store_enumerate_cache(bool (*cb)(const IconCacheInfo* info, void* ctx), void* ctx) {
    cache_lock();
    for (uint16_t i = 0; i < g_count; i++) {
        const lv_draw_buf_t* buf = g_entries[i].draw_buf;
        IconCacheInfo info;
        info.id = g_entries[i].id;
        info.kind = g_entries[i].kind;
        info.w = buf->header.w;
        info.h = buf->header.h;
        info.data_size = buf->data_size;
        if (!cb(&info, ctx)) break;
    }
    cache_unlock();
}

#endif // HAS_DISPLAY
