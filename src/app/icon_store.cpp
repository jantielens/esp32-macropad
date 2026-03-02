#include "icon_store.h"

#if HAS_DISPLAY

#include "log_manager.h"
#include "pad_config.h"

#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <string.h>

// LVGL's built-in lodepng for PNG decoding
#define LODEPNG_NO_COMPILE_CPP
#include <libs/lodepng/lodepng.h>

#define TAG "IconStore"

// ============================================================================
// Cache — growable list in PSRAM
// ============================================================================

struct IconEntry {
    char id[CONFIG_ICON_ID_MAX_LEN];
    lv_draw_buf_t* draw_buf;  // LVGL draw buffer (never freed — LVGL holds refs)
    IconKind kind;
};

static IconEntry* g_entries = nullptr;
static uint16_t g_count = 0;
static uint16_t g_capacity = 0;

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

static int find_entry(const char* id) {
    for (uint16_t i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].id, id) == 0) return (int)i;
    }
    return -1;
}

static bool validate_png(const uint8_t* data, size_t len) {
    // PNG signature: 89 50 4E 47 0D 0A 1A 0A
    if (len < 8) return false;
    return (data[0] == 0x89 && data[1] == 0x50 &&
            data[2] == 0x4E && data[3] == 0x47);
}

// Convert RGBA8888 to planar RGB565A8. Returns PSRAM-allocated buffer
// (ownership transfers to caller). Returns nullptr on OOM.
static uint8_t* rgba_to_rgb565a8(const uint8_t* rgba, uint32_t w, uint32_t h) {
    uint32_t pixel_count = w * h;
    size_t out_size = (size_t)pixel_count * 3;

    uint8_t* out = nullptr;
    if (psramFound()) {
        out = (uint8_t*)heap_caps_malloc(out_size,
                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!out) out = (uint8_t*)malloc(out_size);
    if (!out) return nullptr;

    uint8_t* rgb565 = out;
    uint8_t* alpha = out + pixel_count * 2;

    for (uint32_t i = 0; i < pixel_count; i++) {
        const uint8_t r = rgba[i * 4 + 0];
        const uint8_t g = rgba[i * 4 + 1];
        const uint8_t b = rgba[i * 4 + 2];
        alpha[i] = rgba[i * 4 + 3];

        const uint16_t rgb = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        rgb565[i * 2 + 0] = rgb & 0xFF;
        rgb565[i * 2 + 1] = (rgb >> 8) & 0xFF;
    }

    return out;
}

// Determine icon kind from the semantic icon_id string.
static IconKind kind_from_icon_id(const char* icon_id) {
    if (icon_id && strncmp(icon_id, "mi_", 3) == 0) return ICON_KIND_MONO;
    return ICON_KIND_COLOR;
}

// Create an LVGL draw buffer from raw RGB565A8 data.
// Copies pixel data into a properly aligned LVGL-managed buffer.
// Frees the input `pixels` buffer after copying.
static lv_draw_buf_t* create_draw_buf(uint8_t* pixels, uint32_t w, uint32_t h) {
    lv_draw_buf_t* buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_RGB565A8, 0);
    if (!buf) {
        free(pixels);
        return nullptr;
    }

    // Copy RGB565A8 planar data into LVGL's aligned buffer
    uint32_t data_len = w * h * 3;
    lv_memcpy(buf->data, pixels, data_len);
    free(pixels);

    return buf;
}

// Add or update a cache entry. Takes ownership of pre-allocated RGB565A8 data.
// On failure, frees the pixels buffer.
static bool cache_entry(const char* id, IconKind kind,
                        uint8_t* pixels, uint32_t w, uint32_t h) {
    lv_draw_buf_t* buf = create_draw_buf(pixels, w, h);
    if (!buf) return false;

    // Check if entry already exists (update in place)
    int idx = find_entry(id);
    if (idx >= 0) {
        // Old draw_buf is orphaned — can't free because LVGL may reference it.
        // This is expected on re-install during pad save.
        IconEntry& e = g_entries[idx];
        e.draw_buf = buf;
        e.kind = kind;
        LOGD(TAG, "Updated cache: '%s' %ux%u stride=%u", id, w, h,
             (unsigned)buf->header.stride);
        return true;
    }

    // New entry
    if (!ensure_capacity()) {
        lv_draw_buf_destroy(buf);
        return false;
    }

    IconEntry& e = g_entries[g_count];
    strlcpy(e.id, id, CONFIG_ICON_ID_MAX_LEN);
    e.draw_buf = buf;
    e.kind = kind;

    g_count++;
    LOGD(TAG, "Cached: '%s' %ux%u stride=%u kind=%u (%u total)",
         id, w, h, (unsigned)buf->header.stride, kind, g_count);
    return true;
}

// Load an icon PNG from LittleFS, decode, and cache.
static bool load_from_fs(const char* id, IconKind kind) {
    char path[64];
    snprintf(path, sizeof(path), "/icons/%s.png", id);

    if (!LittleFS.exists(path)) return false;

    File f = LittleFS.open(path, "r");
    if (!f) return false;

    size_t file_size = f.size();
    if (file_size < 8 || file_size > ICON_MAX_PNG_SIZE) {
        f.close();
        LOGW(TAG, "Invalid file size for '%s': %u", id, (unsigned)file_size);
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
        LOGE(TAG, "OOM reading '%s' (%u bytes)", id, (unsigned)file_size);
        return false;
    }

    size_t read = f.readBytes((char*)buf, file_size);
    f.close();

    if (read != file_size) {
        free(buf);
        LOGW(TAG, "Short read for '%s': %u/%u", id, (unsigned)read, (unsigned)file_size);
        return false;
    }

    if (!validate_png(buf, file_size)) {
        free(buf);
        LOGW(TAG, "Invalid PNG in '%s'", id);
        return false;
    }

    // Decode PNG -> RGBA8888
    unsigned char* rgba = nullptr;
    unsigned w = 0, h = 0;
    unsigned err = lodepng_decode32(&rgba, &w, &h, buf, file_size);
    free(buf);

    if (err || !rgba) {
        LOGW(TAG, "PNG decode failed for '%s': %s", id,
             err ? lodepng_error_text(err) : "null output");
        if (rgba) lv_free(rgba);
        return false;
    }

    if (w == 0 || h == 0 || w > ICON_MAX_DIMENSION || h > ICON_MAX_DIMENSION) {
        LOGW(TAG, "PNG dims invalid for '%s': %ux%u", id, w, h);
        lv_free(rgba);
        return false;
    }

    // Convert RGBA -> planar RGB565A8
    uint8_t* pixels = rgba_to_rgb565a8(rgba, w, h);
    lv_free(rgba);

    if (!pixels) {
        LOGE(TAG, "OOM converting '%s' %ux%u", id, w, h);
        return false;
    }

    return cache_entry(id, kind, pixels, w, h);
}

// ============================================================================
// Public API
// ============================================================================

void icon_store_build_key(uint8_t page, uint8_t col, uint8_t row,
                          char* out, size_t out_size) {
    snprintf(out, out_size, "pad_%u_%u_%u", page, col, row);
}

void icon_store_init() {
    if (!LittleFS.exists("/icons")) {
        LittleFS.mkdir("/icons");
        LOGI(TAG, "Created /icons directory");
    }
    LOGI(TAG, "Initialized");
}

bool icon_store_install(const char* id, IconKind kind,
                        const uint8_t* png_data, size_t png_len) {
    if (!id || !id[0] || !png_data || png_len < 8) return false;

    if (!validate_png(png_data, png_len)) {
        LOGW(TAG, "Install rejected: not a valid PNG for '%s'", id);
        return false;
    }

    // Write PNG to LittleFS
    char path[64];
    snprintf(path, sizeof(path), "/icons/%s.png", id);

    File f = LittleFS.open(path, "w");
    if (!f) {
        LOGE(TAG, "Failed to open '%s' for writing", path);
        return false;
    }

    size_t written = f.write(png_data, png_len);
    f.close();

    if (written != png_len) {
        LOGE(TAG, "Short write for '%s': %u/%u", path, (unsigned)written, (unsigned)png_len);
        return false;
    }

    // Decode PNG -> RGBA8888
    unsigned char* rgba = nullptr;
    unsigned w = 0, h = 0;
    unsigned err = lodepng_decode32(&rgba, &w, &h, png_data, png_len);
    if (err || !rgba) {
        LOGW(TAG, "PNG decode failed for '%s': %s (saved to disk)",
             id, err ? lodepng_error_text(err) : "null output");
        if (rgba) lv_free(rgba);
        return true;  // File saved; will decode on next preload
    }

    if (w == 0 || h == 0 || w > ICON_MAX_DIMENSION || h > ICON_MAX_DIMENSION) {
        LOGW(TAG, "PNG dims invalid: %ux%u (saved to disk)", w, h);
        lv_free(rgba);
        return true;
    }

    // Convert RGBA -> planar RGB565A8
    uint8_t* pixels = rgba_to_rgb565a8(rgba, w, h);
    lv_free(rgba);

    if (!pixels) {
        LOGW(TAG, "OOM converting '%s' — saved to disk", id);
        return true;
    }

    if (!cache_entry(id, kind, pixels, w, h)) {
        // cache_entry frees pixels on failure
        LOGW(TAG, "Cache failed for '%s' — saved to disk", id);
        return true;
    }

    LOGI(TAG, "Installed: '%s' %ux%u (PNG %u bytes -> %u bytes cached)",
         id, w, h, (unsigned)png_len, w * h * 3);
    return true;
}

bool icon_store_lookup(const char* id, IconRef* out) {
    if (!id || !id[0] || !out) return false;

    int idx = find_entry(id);
    if (idx < 0) return false;

    out->dsc = (const lv_image_dsc_t*)g_entries[idx].draw_buf;
    out->kind = g_entries[idx].kind;
    return true;
}

void icon_store_preload_pad_pages() {
    uint16_t loaded = 0;

    for (uint8_t page = 0; page < MAX_PAD_PAGES; page++) {
        PadPageConfig* cfg = (PadPageConfig*)heap_caps_malloc(
            sizeof(PadPageConfig), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!cfg) cfg = (PadPageConfig*)malloc(sizeof(PadPageConfig));
        if (!cfg) continue;

        memset(cfg, 0, sizeof(PadPageConfig));
        if (!pad_config_load(page, cfg)) {
            free(cfg);
            continue;
        }

        for (uint8_t i = 0; i < cfg->button_count; i++) {
            if (!cfg->buttons[i].icon_id[0]) continue;

            char key[CONFIG_ICON_ID_MAX_LEN];
            icon_store_build_key(page, cfg->buttons[i].col,
                                 cfg->buttons[i].row, key, sizeof(key));

            if (find_entry(key) >= 0) continue;  // Already cached
            IconKind kind = kind_from_icon_id(cfg->buttons[i].icon_id);
            if (load_from_fs(key, kind)) loaded++;
        }

        free(cfg);
    }

    LOGI(TAG, "Preload complete: %u icons loaded, %u total cached",
         loaded, g_count);
}

void icon_store_preload_page(uint8_t page) {
    if (page >= MAX_PAD_PAGES) return;

    PadPageConfig* cfg = (PadPageConfig*)heap_caps_malloc(
        sizeof(PadPageConfig), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!cfg) cfg = (PadPageConfig*)malloc(sizeof(PadPageConfig));
    if (!cfg) return;

    memset(cfg, 0, sizeof(PadPageConfig));
    if (!pad_config_load(page, cfg)) {
        free(cfg);
        return;
    }

    uint16_t loaded = 0;
    for (uint8_t i = 0; i < cfg->button_count; i++) {
        if (!cfg->buttons[i].icon_id[0]) continue;

        char key[CONFIG_ICON_ID_MAX_LEN];
        icon_store_build_key(page, cfg->buttons[i].col,
                             cfg->buttons[i].row, key, sizeof(key));

        // Always reload — icon may have been re-installed
        IconKind kind = kind_from_icon_id(cfg->buttons[i].icon_id);
        if (load_from_fs(key, kind)) loaded++;
    }

    free(cfg);
    if (loaded > 0) {
        LOGI(TAG, "Page %u preload: %u icons", page, loaded);
    }
}

void icon_store_delete_page_icons(uint8_t page) {
    if (page >= MAX_PAD_PAGES) return;

    char prefix[16];
    snprintf(prefix, sizeof(prefix), "pad_%u_", page);
    size_t prefix_len = strlen(prefix);

    File dir = LittleFS.open("/icons");
    if (!dir || !dir.isDirectory()) return;

    // Collect filenames first (can't delete while iterating)
    char to_delete[MAX_PAD_BUTTONS][32];
    uint8_t del_count = 0;

    File entry = dir.openNextFile();
    while (entry && del_count < MAX_PAD_BUTTONS) {
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
        LittleFS.remove(path);
    }

    if (del_count > 0) {
        LOGI(TAG, "Deleted %u icon files for page %u", del_count, page);
    }
}

uint16_t icon_store_cache_count() {
    return g_count;
}

void icon_store_enumerate_cache(bool (*cb)(const IconCacheInfo* info, void* ctx), void* ctx) {
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
}

#endif // HAS_DISPLAY
