#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include "pad_config.h"
#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

// ============================================================================
// Icon Store — runtime icon management for pad buttons
// ============================================================================
//
// Icons are installed via the REST API as PNG files, stored through Storage
// at /icons/<key>.png, and cached in PSRAM for LVGL rendering.
//
// Generated PNG keys are positional: "pad_<page>_<layer>_<col>_<row>".
// ScreenButtonConfig retains only the source ID and explicit render kind.
//
// Storage format: PNG files on LittleFS at /icons/<key>.png.
// On load, PNG is decoded by LVGL's bundled lodepng into an ARGB8888
// lv_draw_buf_t and cached in PSRAM.
//
// Cache entries are retained while their pad's LVGL image objects use them.

#ifndef ICON_MAX_DIMENSION
#define ICON_MAX_DIMENSION      720
#endif
#define ICON_MAX_PNG_SIZE       (512 * 1024)
#define ICON_MAX_SOURCE_JSON_SIZE (704 * 1024)
#define ICON_MAX_CATALOG_SOURCE_JSON_SIZE (64 * 1024)

// Icon kind — affects LVGL rendering (recolor for mono icons)
enum IconKind : uint8_t {
    ICON_KIND_COLOR = 0,  // Emoji — true-color + alpha, no recolor
    ICON_KIND_MONO  = 1,  // Material Icon — mono + alpha, LVGL recolor applied
};

enum IconStoreResult : uint8_t {
    ICON_STORE_OK,
    ICON_STORE_INVALID,
    ICON_STORE_TOO_LARGE,
    ICON_STORE_BUSY,
    ICON_STORE_UNAVAILABLE,
    ICON_STORE_IO_ERROR,
    ICON_STORE_CONFLICT,
};

// Reference to a cached icon (returned by lookup)
struct IconRef {
    const lv_image_dsc_t* dsc;  // LVGL image descriptor (valid for lifetime of cache)
    IconKind kind;               // Color or Mono — caller applies recolor if mono
};

struct IconArtifactInfo {
    char source_id[CONFIG_ICON_SOURCE_ID_MAX_LEN];
    IconKind kind;
    uint32_t width;
    uint32_t height;
    uint32_t render_key;
};

#ifdef __cplusplus
extern "C" {
#endif

// Initialize icon store. Creates /icons/ directory if needed.
// Call once from setup() after pad_config_init().
void icon_store_init();

// Retain validated catalog SVG JSON under a generated
// device-local source ID.
IconStoreResult icon_store_source_install(const uint8_t* json, size_t json_len,
                                          char* source_id, size_t source_id_len);
struct IconSourceUpload;
IconStoreResult icon_store_source_png_begin(size_t total, IconSourceUpload** upload);
IconStoreResult icon_store_source_png_write(IconSourceUpload* upload, const uint8_t* data, size_t length);
IconStoreResult icon_store_source_png_finish(IconSourceUpload* upload, char* source_id, size_t source_id_len);
void icon_store_source_png_abort(IconSourceUpload* upload);
bool icon_store_source_path(const char* source_id, char* path, size_t capacity, bool* png);
bool icon_store_source_id_valid(const char* source_id);
bool icon_store_key_valid(const char* key);
bool icon_store_source_exists(const char* source_id);
bool icon_store_source_matches_kind(const char* source_id, IconKind kind);
bool icon_store_source_matches_metadata(const char* source_id,
                                        const char* catalog_id,
                                        const char* kind);
bool icon_store_artifact_matches(const char* key, const char* source_id, IconKind kind,
                                 uint32_t render_key);
bool icon_store_read_artifact(const char* key, IconArtifactInfo* out);
bool icon_store_source_delete_if_unreferenced(const char* source_id);
void icon_store_cleanup_unreferenced_sources();

// Install a generated PNG and its source/kind/dimension metadata.
IconStoreResult icon_store_install(const char* key, IconKind kind,
                                   const char* source_id, uint32_t render_key,
                                   const uint8_t* png_data, size_t png_len);

// Look up a cached icon by id. Returns true if found (ref is populated).
// Only returns icons already in the cache — does NOT load from flash.
bool icon_store_lookup(const char* id, IconRef* out);

// Preload icons for all pad pages. Scans pad configs for source references,
// computes positional keys, and loads matching generated files.
// Call from setup() after pad_config_init() and icon_store_init().
void icon_store_preload_pad_pages();

// Preload icons for a single pad page (including template fallback).
// Safe to call after pad config changes — refreshes cache entries from the
// current on-disk/page-template source for each icon-bearing button.
void icon_store_preload_pad(uint8_t page);

// Delete one generated PNG and retire its cached buffer for the owning page.
bool icon_store_delete_file(const char* filename);

// Delete all icon files for a page (pad_<page>_*.png).
// Reserved for explicit pad deletion/reset.
void icon_store_delete_page_icons(uint8_t page);

// Release buffers retired by replacements for a pad after its LVGL tiles have
// been deleted. Must be called from the LVGL task.
void icon_store_collect_retired(uint8_t page);

// Return the number of icons currently in the cache.
uint16_t icon_store_cache_count();

// Info about a single cached icon (for debug API)
struct IconCacheInfo {
    const char* id;
    IconKind kind;
    uint32_t w;
    uint32_t h;
    uint32_t data_size;
};

// Iterate cached icons. Calls cb for each entry. Stops if cb returns false.
void icon_store_enumerate_cache(bool (*cb)(const IconCacheInfo* info, void* ctx), void* ctx);

// Build a positional icon key for a button: "pad_<page>_<layer>_<col>_<row>"
void icon_store_build_key(uint8_t page, const char* layer, uint8_t col, uint8_t row,
                          char* out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif // HAS_DISPLAY
