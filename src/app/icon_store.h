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
// Icons are installed via the REST API as PNG files, stored on
// LittleFS at /icons/<id>.png, and cached in PSRAM for LVGL rendering.
//
// Icon IDs are positional: "pad_<page>_<col>_<row>" — deterministic filenames
// that are overwritten on each pad save. The ScreenButtonConfig.icon_id field
// holds the semantic name (e.g. "emoji_sun", "mi_power") used by the browser
// to decide what to render; the firmware only uses positional keys.
//
// Storage format: PNG files on LittleFS at /icons/<key>.png.
// On load, PNG is decoded by LVGL's bundled lodepng into an ARGB8888
// lv_draw_buf_t and cached in PSRAM.
//
// Cache is a non-evicting growable list in PSRAM. Entries are never freed
// because LVGL image objects hold raw pointers to cached pixel data.

#define ICON_MAX_DIMENSION      720
#define ICON_MAX_PNG_SIZE       (512 * 1024)

// Icon kind — affects LVGL rendering (recolor for mono icons)
enum IconKind : uint8_t {
    ICON_KIND_COLOR = 0,  // Emoji — true-color + alpha, no recolor
    ICON_KIND_MONO  = 1,  // Material Icon — mono + alpha, LVGL recolor applied
};

// Reference to a cached icon (returned by lookup)
struct IconRef {
    const lv_image_dsc_t* dsc;  // LVGL image descriptor (valid for lifetime of cache)
    IconKind kind;               // Color or Mono — caller applies recolor if mono
};

#ifdef __cplusplus
extern "C" {
#endif

// Initialize icon store. Creates /icons/ directory if needed.
// Call once from setup() after pad_config_init().
void icon_store_init();

// Install a PNG icon. Writes to LittleFS, decodes, and caches.
// Returns true on success. id must be a valid icon key (e.g. "pad_0_1_2").
bool icon_store_install(const char* id, IconKind kind, const uint8_t* png_data, size_t png_len);

// Look up a cached icon by id. Returns true if found (ref is populated).
// Only returns icons already in the cache — does NOT load from flash.
bool icon_store_lookup(const char* id, IconRef* out);

// Preload icons for all pad pages. Scans pad configs for buttons with
// icon_id set, computes positional keys, loads matching files from LittleFS.
// Call from setup() after pad_config_init() and icon_store_init().
void icon_store_preload_pad_pages();

// Preload icons for a single pad page. Call after saving a page config
// to ensure new icons are available to the LVGL task.
void icon_store_preload_page(uint8_t page);

// Delete all icon files for a page (pad_<page>_*.png).
// Call before installing new icons when a pad page is saved.
void icon_store_delete_page_icons(uint8_t page);

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

// Build a positional icon key for a button: "pad_<page>_<col>_<row>"
void icon_store_build_key(uint8_t page, uint8_t col, uint8_t row,
                          char* out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif // HAS_DISPLAY
