#ifndef PAD_SCREEN_H
#define PAD_SCREEN_H

#include "screen.h"
#include "../pad_config.h"
#include "../pad_layout.h"
#if HAS_IMAGE_FETCH
#include "../image_fetch.h"
#endif
#include <lvgl.h>

class DisplayManager;

// ============================================================================
// Pad Screen
// ============================================================================
// Renders a single pad page (0–7) with grid-based button tiles.
// Config is loaded lazily from LittleFS on first update() and when the
// generation counter changes.

// Runtime MQTT label binding (links an LVGL label to a subscription topic)
struct RuntimeLabelBinding {
    lv_obj_t* label;                                  // LVGL label to update
    char mqtt_topic[CONFIG_MQTT_TOPIC_MAX_LEN];       // topic to poll from store
    char json_path[CONFIG_JSON_PATH_MAX_LEN];         // extraction path
    char format[CONFIG_FORMAT_MAX_LEN];               // printf format string
    bool active;                                      // binding is in use
};

// Runtime toggle state binding (links a tile's fg color to an MQTT state)
struct RuntimeStateBinding {
    uint8_t tileIndex;                                // index into tiles[]
    char mqtt_topic[CONFIG_MQTT_TOPIC_MAX_LEN];       // topic to poll
    char json_path[CONFIG_JSON_PATH_MAX_LEN];         // extraction path
    char on_value[CONFIG_STATE_ON_VALUE_MAX_LEN];     // value == ON
    uint32_t fg_color_rgb;                            // normal fg
    uint32_t disabled_fg_color_rgb;                   // dimmed fg (OFF)
    bool currentlyOn;                                 // current visual state
    bool initialized;                                 // received at least one value
    bool active;
};

// Runtime state per button tile (kept in memory while screen is active)
struct ButtonTile {
    lv_obj_t* obj;            // Button container object
    lv_obj_t* label_top;      // Top label (Font S) or nullptr
    lv_obj_t* label_center;   // Center label (Font L) or nullptr
    lv_obj_t* label_bottom;   // Bottom label (Font S) or nullptr
    lv_obj_t* icon_img;       // Icon image widget (or nullptr)
    uint32_t bg_color_rgb;    // Original bg color (for tap flash restore)
    uint8_t page;             // Page index (for HA event)
    uint8_t col;              // Grid column (for HA event)
    uint8_t row;              // Grid row (for HA event)
    ButtonAction action;      // Tap action
    ButtonAction lp_action;   // Long-press action
#if HAS_IMAGE_FETCH
    lv_obj_t* bg_image;       // Background image widget (or nullptr)
    image_slot_t image_slot;  // Image fetch slot (-1 = none)
    lv_image_dsc_t img_dsc;   // LVGL image descriptor for current frame
    uint16_t* owned_pixels;   // Tile-owned copy of pixel data (PSRAM)
    size_t owned_pixels_size; // Byte size of owned_pixels buffer
#endif
};

class PadScreen : public Screen {
private:
    uint8_t pageIndex;
    DisplayManager* displayMgr;

    lv_obj_t* screen;
    lv_obj_t* container;       // Content container (offset by pixel shift margin)

    ButtonTile tiles[MAX_PAD_BUTTONS];
    uint8_t tileCount;

    // MQTT label bindings (max 3 per button * MAX_PAD_BUTTONS)
    static const int MAX_BINDINGS = MAX_PAD_BUTTONS * 3;
    RuntimeLabelBinding bindings[MAX_PAD_BUTTONS * 3];
    uint16_t bindingCount;

    // Toggle state bindings (max 1 per button)
    RuntimeStateBinding stateBindings[MAX_PAD_BUTTONS];
    uint16_t stateBindingCount;

    uint32_t cachedGeneration; // Last seen pad_config generation
    bool tilesBuilt;

    // Build/destroy tile LVGL objects from config
    void buildTiles();
    void clearTiles();
    void pollMqttBindings();
    void pollToggleState();
#if HAS_IMAGE_FETCH
    void pollImageFrames();
#endif

    // Event callbacks
    static void onTap(lv_event_t* e);
    static void onLongPress(lv_event_t* e);
    // Touch navigation for screen switching
    static void onSwipe(lv_event_t* e);

public:
    static void tapFlashTimerCb(lv_timer_t* timer);
    PadScreen(uint8_t page, DisplayManager* manager);
    ~PadScreen();

    void create() override;
    void destroy() override;
    void show() override;
    void hide() override;
    void update() override;

    uint8_t getPageIndex() const { return pageIndex; }
};

#endif // PAD_SCREEN_H
