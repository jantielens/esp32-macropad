#ifndef PAD_SCREEN_H
#define PAD_SCREEN_H

#include "screen.h"
#include "../pad_config.h"
#include "../pad_layout.h"
#include "../binding_template.h"
#include "../widgets/widget.h"
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

// Runtime MQTT label binding — template-based
// Labels containing [scheme:params] tokens are resolved each poll cycle.
struct RuntimeLabelBinding {
    lv_obj_t* label;                                  // LVGL label to update
    char templ[CONFIG_LABEL_MAX_LEN];                 // Original label text (template)
    char last[BINDING_TEMPLATE_MAX_LEN];              // Last rendered result (skip if unchanged)
    bool active;
};

// Runtime color binding — a color field that may contain binding templates.
// Resolved each poll cycle; only applies LVGL style on change.
struct RuntimeColorBinding {
    uint8_t tileIndex;                                // index into tiles[]
    char templ[CONFIG_COLOR_MAX_LEN];                 // Original color string (template or static)
    uint32_t defaultColor;                            // Fallback color when unresolved
    uint32_t lastApplied;                             // Last applied color (skip if unchanged)
    uint8_t target;                                   // 0=bg, 1=fg (labels+icon recolor), 2=border
    bool active;
    bool hasBindings;                                 // true if template contains [xxx:...] tokens
};

// Tri-state button state: enabled (interactive), disabled (visible, no input), hidden
enum BtnState : uint8_t {
    BTN_STATE_ENABLED  = 0,
    BTN_STATE_DISABLED = 1,
    BTN_STATE_HIDDEN   = 2,
};

// Runtime button state binding — resolved each poll cycle to determine visibility/interactivity.
struct RuntimeBtnStateBinding {
    uint8_t tileIndex;                                // index into tiles[]
    char templ[CONFIG_BTN_STATE_MAX_LEN];             // Original btn_state string (template or static)
    uint8_t lastState;                                // Last applied BtnState (0xFF = uninitialized)
    bool active;
    bool hasBindings;                                 // true if template contains [xxx:...] tokens
};

// Runtime state per button tile (kept in memory while screen is active)
struct ButtonTile {
    lv_obj_t* obj;            // Button container object
    lv_obj_t* label_top;      // Top label (Font S) or nullptr
    lv_obj_t* label_center;   // Center label (Font L) or nullptr
    lv_obj_t* label_bottom;   // Bottom label (Font S) or nullptr
    lv_obj_t* icon_img;       // Icon image widget (or nullptr)
    bool icon_is_mono;        // True if icon uses fg recolor
    uint8_t page;             // Page index (for HA event)
    uint8_t col;              // Grid column (for HA event)
    uint8_t row;              // Grid row (for HA event)
    ButtonAction action;      // Tap action
    ButtonAction lp_action;   // Long-press action
    // Widget runtime state (non-null widget_type = this tile is a widget)
    const WidgetType* widget_type;
    WidgetConfig widget_cfg;   // Copy of config (needed for update calls)
    WidgetState widget_state;
    // Widget data binding templates (primary + optional middle/inner ring for gauge)
    char widget_binding[CONFIG_LABEL_MAX_LEN];
    char widget_last[BINDING_TEMPLATE_MAX_LEN * 3 + 4]; // Last resolved combined value (dedup)
    // Optional extra bindings for multi-ring gauges (middle/inner rings)
    char widget_binding_2[CONFIG_LABEL_MAX_LEN];
    char widget_binding_3[CONFIG_LABEL_MAX_LEN];
    lv_obj_t* tap_overlay;    // Semi-transparent overlay shown briefly on tap
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

    // Color bindings (max 3 per button: bg, fg, border + 1 page bg)
    static const int MAX_COLOR_BINDINGS = MAX_PAD_BUTTONS * 3 + 1;
    RuntimeColorBinding colorBindings[MAX_PAD_BUTTONS * 3 + 1];
    uint16_t colorBindingCount;

    // Button state bindings (1 per button max)
    RuntimeBtnStateBinding btnStateBindings[MAX_PAD_BUTTONS];
    uint16_t btnStateBindingCount;

    uint32_t cachedGeneration; // Last seen pad_config generation
    bool tilesBuilt;
    char wakeScreen[CONFIG_SCREEN_ID_MAX_LEN]; // Cached wake_screen from config
    char pageBgTemplate[CONFIG_COLOR_MAX_LEN];     // Page background color/binding
    uint32_t pageBgDefault;                        // Fallback page bg color

    // Build/destroy tile LVGL objects from config
    void buildTiles();
    void clearTiles();
    void pollMqttBindings();
    void pollColorBindings();
    void pollBtnStateBindings();
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
    const char* wakeScreenId() const override;

    uint8_t getPageIndex() const { return pageIndex; }
};

#endif // PAD_SCREEN_H
