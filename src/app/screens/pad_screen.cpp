#include "pad_screen.h"
#include "../display_manager.h"
#include "../icon_store.h"
#include "../log_manager.h"
#include "../pad_config.h"
#include "../pad_layout.h"
#if HAS_MQTT
#include "../mqtt_manager.h"
#include "../mqtt_sub_store.h"
#include "../pad_binding.h"
#include <ArduinoJson.h>
#endif
#if HAS_IMAGE_FETCH
#include "../image_fetch.h"
#endif

#include <esp_heap_caps.h>
#include <string.h>

#define TAG "PadScr"

// Tap flash overlay: semi-transparent darken/lighten for 100ms
#define TAP_FLASH_DURATION_MS  100
#define TAP_OVERLAY_DARK_OPA   80   // ~31% black overlay on light backgrounds
#define TAP_OVERLAY_LIGHT_OPA  50   // ~20% white overlay on dark backgrounds
#define TAP_LUMINANCE_THRESH   180  // Perceived-brightness cutoff (0-255)
#define TILE_PAD_PX            4    // Content padding inside each tile

// ============================================================================
// Helpers
// ============================================================================
// These static helpers are defined here first and are visible to all
// subsequently-included translation-unit files (pad_tile_builder.cpp,
// pad_screen_events.cpp, pad_screen_poll.cpp) via screens.cpp #include chain.

// Perceived luminance (ITU-R BT.601) of an RGB888 color (0-255 range)
static uint8_t perceived_luminance(uint32_t rgb) {
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
}

static lv_color_t rgb_to_lv(uint32_t rgb) {
    return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// parse_hex_color() from pad_config.h is used for resolved color strings

// ============================================================================
// PadScreen
// ============================================================================

PadScreen::PadScreen(uint8_t page, DisplayManager* manager)
    : pageIndex(page), displayMgr(manager), screen(nullptr), container(nullptr),
      tiles(nullptr), tileCount(0),
      bindings(nullptr), bindingCount(0),
      colorBindings(nullptr), colorBindingCount(0),
      numberBindings(nullptr), numberBindingCount(0),
      btnStateBindings(nullptr), btnStateBindingCount(0),
      pageBindings(nullptr), pageBindingCount(0),
      arraysAllocated(false),
      cachedGeneration(UINT32_MAX), tilesBuilt(false) {
    wakeScreen[0] = '\0';
    pageBgTemplate[0] = '\0';
    pageBgDefault = 0x000000;
}

PadScreen::~PadScreen() {
    destroy();
    freeArrays();
}

// Allocate the heavy binding arrays in PSRAM (fallback to heap).
bool PadScreen::allocateArrays() {
    if (arraysAllocated) return true;

    #define PAD_ALLOC(ptr, type, count) do { \
        ptr = (type*)heap_caps_calloc(count, sizeof(type), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); \
        if (!ptr) ptr = (type*)calloc(count, sizeof(type)); \
        if (!ptr) { freeArrays(); return false; } \
    } while(0)

    PAD_ALLOC(tiles,            ButtonTile,             MAX_PAD_BUTTONS);
    PAD_ALLOC(bindings,         RuntimeLabelBinding,    MAX_BINDINGS);
    PAD_ALLOC(colorBindings,    RuntimeColorBinding,    MAX_COLOR_BINDINGS);
    PAD_ALLOC(numberBindings,   RuntimeNumberBinding,   MAX_NUMBER_BINDINGS);
    PAD_ALLOC(btnStateBindings, RuntimeBtnStateBinding, MAX_PAD_BUTTONS);
    PAD_ALLOC(pageBindings,     PadBinding,             PAD_MAX_BINDINGS);

    #undef PAD_ALLOC

    arraysAllocated = true;
    LOGD(TAG, "Pad %u: arrays allocated", pageIndex);
    return true;
}

// Free the heavy binding arrays (called on eviction or destruction).
void PadScreen::freeArrays() {
    if (!arraysAllocated) return;

    free(tiles);            tiles = nullptr;
    free(bindings);         bindings = nullptr;
    free(colorBindings);    colorBindings = nullptr;
    free(numberBindings);   numberBindings = nullptr;
    free(btnStateBindings); btnStateBindings = nullptr;
    free(pageBindings);     pageBindings = nullptr;

    tileCount = 0;
    bindingCount = 0;
    colorBindingCount = 0;
    numberBindingCount = 0;
    btnStateBindingCount = 0;
    pageBindingCount = 0;

    arraysAllocated = false;
    tilesBuilt = false;
    cachedGeneration = UINT32_MAX;
    LOGD(TAG, "Pad %u: arrays freed", pageIndex);
}

void PadScreen::evict() {
    clearTiles();
    freeArrays();
}

const char* PadScreen::wakeScreenId() const {
    return wakeScreen[0] ? wakeScreen : nullptr;
}

void PadScreen::create() {
    if (screen) return;

    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, rgb_to_lv(pageBgDefault), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);

    // Content container fills entire screen — tiles positioned absolutely within
    container = lv_obj_create(screen);
    lv_obj_set_size(container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    // Swipe gesture for page navigation
    lv_obj_add_event_cb(screen, onSwipe, LV_EVENT_GESTURE, this);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
}

void PadScreen::destroy() {
    clearTiles();
    if (screen) {
        lv_obj_delete(screen);
        screen = nullptr;
        container = nullptr;
    }
}

void PadScreen::show() {
    if (screen) {
        lv_screen_load(screen);
    }

    // Clear last-rendered text so the first poll after navigating back
    // re-renders current store values.
    for (uint16_t i = 0; i < bindingCount; i++) {
        bindings[i].last[0] = '\0';
    }
    for (uint16_t i = 0; i < colorBindingCount; i++) {
        colorBindings[i].lastApplied = UINT32_MAX; // Force re-apply
    }
    for (uint16_t i = 0; i < numberBindingCount; i++) {
        numberBindings[i].lastApplied = INT16_MIN; // Force re-apply
    }

#if HAS_IMAGE_FETCH
    for (uint8_t i = 0; i < tileCount; i++) {
        if (tiles[i].image_slot != IMAGE_SLOT_INVALID)
            image_fetch_resume_slot(tiles[i].image_slot);
    }
#endif
}

void PadScreen::hide() {
#if HAS_IMAGE_FETCH
    for (uint8_t i = 0; i < tileCount; i++) {
        if (tiles[i].image_slot != IMAGE_SLOT_INVALID)
            image_fetch_pause_slot(tiles[i].image_slot);
    }
#endif
}

void PadScreen::update() {
    if (!screen) return;

    // Check if config has changed
    uint32_t gen = pad_config_get_generation();
    if (tilesBuilt && gen == cachedGeneration) {
        // Config unchanged — poll bindings in priority order
#if HAS_MQTT
        // Set page context so [pad:] tokens in bindings can resolve
        pad_binding_set_bindings(pageBindings, pageBindingCount);
#endif
        pollBtnStateBindings();   // Visibility/interactivity first
        pollMqttBindings();
        pollColorBindings();
        pollNumberBindings();
        mqtt_sub_store_clear_dirty();
#if HAS_MQTT
        pad_binding_set_bindings(nullptr, 0);
#endif
#if HAS_IMAGE_FETCH
        pollImageFrames();
#endif
        return;
    }

    cachedGeneration = gen;
    buildTiles();
}

// ============================================================================
// Tile Management — see pad_tile_builder.cpp
// ============================================================================
// Event Handlers — see pad_screen_events.cpp
// ============================================================================
// Polling — see pad_screen_poll.cpp
// ============================================================================
