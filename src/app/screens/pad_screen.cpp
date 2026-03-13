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
      tileCount(0), bindingCount(0), colorBindingCount(0), numberBindingCount(0),
      cachedGeneration(UINT32_MAX), tilesBuilt(false) {
    memset(tiles, 0, sizeof(tiles));
    memset(bindings, 0, sizeof(bindings));
    memset(colorBindings, 0, sizeof(colorBindings));
    memset(numberBindings, 0, sizeof(numberBindings));
    wakeScreen[0] = '\0';
    pageBgTemplate[0] = '\0';
    pageBgDefault = 0x000000;
    pageBindingCount = 0;
}

PadScreen::~PadScreen() {
    destroy();
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
// Tile Management
// ============================================================================

void PadScreen::clearTiles() {
    for (uint8_t i = 0; i < tileCount; i++) {
        // Destroy widget state before LVGL objects are deleted
        if (tiles[i].widget_type && tiles[i].widget_type->destroyUI) {
            tiles[i].widget_type->destroyUI(&tiles[i].widget_state);
            tiles[i].widget_type = nullptr;
        }
#if HAS_IMAGE_FETCH
        // Cancel fetch slot first (stops background task from touching buffers)
        if (tiles[i].image_slot != IMAGE_SLOT_INVALID) {
            image_fetch_cancel(tiles[i].image_slot);
            tiles[i].image_slot = IMAGE_SLOT_INVALID;
        }
#endif
        // Delete LVGL objects before freeing pixel data they reference
        if (tiles[i].obj) {
            lv_obj_delete(tiles[i].obj);
            tiles[i].obj = nullptr;
        }
#if HAS_IMAGE_FETCH
        tiles[i].bg_image = nullptr;
        if (tiles[i].owned_pixels) {
            heap_caps_free(tiles[i].owned_pixels);
            tiles[i].owned_pixels = nullptr;
            tiles[i].owned_pixels_size = 0;
        }
        memset(&tiles[i].img_dsc, 0, sizeof(tiles[i].img_dsc));
#endif
    }
    tileCount = 0;
    bindingCount = 0;
    colorBindingCount = 0;
    numberBindingCount = 0;
    btnStateBindingCount = 0;
    tilesBuilt = false;
}

void PadScreen::buildTiles() {
    clearTiles();

    if (!container) return;

    // Allocate PadPageConfig in PSRAM (temporary — freed at end of this function)
    PadPageConfig* cfg = nullptr;
    if (psramFound()) {
        cfg = (PadPageConfig*)heap_caps_malloc(sizeof(PadPageConfig), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!cfg) {
        cfg = (PadPageConfig*)malloc(sizeof(PadPageConfig));
    }
    if (!cfg) {
        LOGE(TAG, "OOM for PadPageConfig");
        return;
    }

    bool loaded = pad_config_load(pageIndex, cfg);
    if (!loaded) {
        wakeScreen[0] = '\0';
        pageBgTemplate[0] = '\0';
        pageBgDefault = 0x000000;
        free(cfg);
        tilesBuilt = true; // Mark built (empty) to avoid retrying every frame
        return;
    }

    // Cache page-level settings
    strlcpy(wakeScreen, cfg->wake_screen, sizeof(wakeScreen));
    strlcpy(pageBgTemplate, cfg->bg_color, sizeof(pageBgTemplate));
    { uint32_t bg = 0x000000; parse_hex_color(cfg->bg_color, &bg); pageBgDefault = bg; }
    if (screen) lv_obj_set_style_bg_color(screen, rgb_to_lv(pageBgDefault), 0);

    // Cache page-level named bindings for [pad:] scheme resolution
    pageBindingCount = cfg->binding_count;
    memcpy(pageBindings, cfg->bindings, cfg->binding_count * sizeof(PadBinding));

    // Only grid layout supported in v0
    if (strcmp(cfg->layout, "grid") != 0) {
        LOGW(TAG, "Page %u: unsupported layout '%s', skipping", pageIndex, cfg->layout);
        free(cfg);
        tilesBuilt = true;
        return;
    }

    if (cfg->button_count == 0) {
        free(cfg);
        tilesBuilt = true;
        return;
    }

    // Get display dimensions
    int disp_w = displayMgr->getActiveWidth();
    int disp_h = displayMgr->getActiveHeight();

    // Compute grid positions
    uint8_t cols_arr[MAX_PAD_BUTTONS];
    uint8_t rows_arr[MAX_PAD_BUTTONS];
    uint8_t cs_arr[MAX_PAD_BUTTONS];
    uint8_t rs_arr[MAX_PAD_BUTTONS];
    PadRect rects[MAX_PAD_BUTTONS];

    for (uint8_t i = 0; i < cfg->button_count; i++) {
        cols_arr[i] = cfg->buttons[i].col;
        rows_arr[i] = cfg->buttons[i].row;
        cs_arr[i] = cfg->buttons[i].col_span;
        rs_arr[i] = cfg->buttons[i].row_span;
    }

    pad_compute_grid(
        cfg->cols, cfg->rows,
        (uint16_t)disp_w, (uint16_t)disp_h,
        cols_arr, rows_arr, cs_arr, rs_arr,
        cfg->button_count, rects);

    const UIScaleInfo& scale = pad_get_scale_info();

    // Create button tiles
    for (uint8_t i = 0; i < cfg->button_count && i < MAX_PAD_BUTTONS; i++) {
        const ScreenButtonConfig& bcfg = cfg->buttons[i];
        const PadRect& r = rects[i];
        ButtonTile& tile = tiles[i];

        // Create tile container
        lv_obj_t* obj = lv_obj_create(container);
        lv_obj_set_pos(obj, r.x, r.y);
        lv_obj_set_size(obj, r.w, r.h);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

        // Styling — compute initial colors from config strings
        uint32_t bg_def = 0x333333; parse_hex_color(bcfg.bg_color, &bg_def);
        uint32_t fg_def = 0xFFFFFF; parse_hex_color(bcfg.fg_color, &fg_def);
        uint32_t border_def = 0x000000; parse_hex_color(bcfg.border_color, &border_def);
        lv_obj_set_style_bg_color(obj, rgb_to_lv(bg_def), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(obj, rgb_to_lv(border_def), 0);
        lv_coord_t bw_def = (lv_coord_t)strtol(bcfg.border_width, nullptr, 10);
        lv_coord_t cr_def = (lv_coord_t)strtol(bcfg.corner_radius, nullptr, 10);
        lv_obj_set_style_border_width(obj, bw_def, 0);
        lv_obj_set_style_radius(obj, cr_def, 0);
        lv_obj_set_style_clip_corner(obj, true, 0);
        lv_obj_set_style_pad_all(obj, TILE_PAD_PX, 0);

        lv_color_t fg = rgb_to_lv(fg_def);

        // Top label
        lv_obj_t* lbl_top = nullptr;
        if (bcfg.label_top[0]) {
            lbl_top = lv_label_create(obj);
            lv_obj_set_style_text_color(lbl_top, pad_resolve_label_color(bcfg.style_top, fg), 0);
            lv_obj_set_style_text_font(lbl_top, pad_resolve_font(bcfg.style_top, scale.font_small), 0);
            lv_obj_set_width(lbl_top, r.w - 8);
            pad_apply_long_mode(lbl_top, bcfg.style_top);
            lv_label_set_text(lbl_top, bcfg.label_top);
            lv_obj_align(lbl_top, LV_ALIGN_TOP_MID, 0, bcfg.style_top.y_offset);
            lv_obj_set_style_text_align(lbl_top, pad_resolve_align(bcfg.style_top), 0);
            lv_obj_clear_flag(lbl_top, LV_OBJ_FLAG_CLICKABLE);
        }

        // Center label (shown when no icon)
        lv_obj_t* lbl_center = nullptr;
        if (bcfg.label_center[0] && !bcfg.icon_id[0]) {
            lbl_center = lv_label_create(obj);
            lv_obj_set_style_text_color(lbl_center, pad_resolve_label_color(bcfg.style_center, fg), 0);
            lv_obj_set_style_text_font(lbl_center, pad_resolve_font(bcfg.style_center, scale.font_large), 0);
            lv_obj_set_width(lbl_center, r.w - 8);
            pad_apply_long_mode(lbl_center, bcfg.style_center);
            lv_label_set_text(lbl_center, bcfg.label_center);
            lv_obj_align(lbl_center, LV_ALIGN_CENTER, 0, bcfg.style_center.y_offset);
            lv_obj_set_style_text_align(lbl_center, pad_resolve_align(bcfg.style_center), 0);
            lv_obj_clear_flag(lbl_center, LV_OBJ_FLAG_CLICKABLE);
        }

        // Icon image (shown when icon_id is set and icon is cached)
        lv_obj_t* icon_img = nullptr;
        if (bcfg.icon_id[0]) {
            char icon_key[CONFIG_ICON_ID_MAX_LEN];
            icon_store_build_key(pageIndex, bcfg.col, bcfg.row,
                                 icon_key, sizeof(icon_key));
            IconRef ref;
            if (icon_store_lookup(icon_key, &ref)) {
                icon_img = lv_image_create(obj);
                lv_image_set_src(icon_img, ref.dsc);

                // Center icon in the space between labels.
                // LV_ALIGN_CENTER is the center of the full content area;
                // offset by half the difference of top/bottom label heights.
                const lv_font_t* top_font = pad_resolve_font(bcfg.style_top, scale.font_small);
                const lv_font_t* bot_font = pad_resolve_font(bcfg.style_bottom, scale.font_small);
                const int16_t top_h = bcfg.label_top[0] ?
                    lv_font_get_line_height(top_font) : 0;
                const int16_t bot_h = bcfg.label_bottom[0] ?
                    lv_font_get_line_height(bot_font) : 0;
                const int16_t y_ofs = (top_h - bot_h) / 2;
                lv_obj_align(icon_img, LV_ALIGN_CENTER, 0, y_ofs);

                lv_obj_clear_flag(icon_img, LV_OBJ_FLAG_CLICKABLE);
                if (ref.kind == ICON_KIND_MONO) {
                    lv_obj_set_style_image_recolor(icon_img, fg, 0);
                    lv_obj_set_style_image_recolor_opa(icon_img, LV_OPA_COVER, 0);
                    tile.icon_is_mono = true;
                }
            }
        }

        // Bottom label
        lv_obj_t* lbl_bottom = nullptr;
        if (bcfg.label_bottom[0]) {
            lbl_bottom = lv_label_create(obj);
            lv_obj_set_style_text_color(lbl_bottom, pad_resolve_label_color(bcfg.style_bottom, fg), 0);
            lv_obj_set_style_text_font(lbl_bottom, pad_resolve_font(bcfg.style_bottom, scale.font_small), 0);
            lv_obj_set_width(lbl_bottom, r.w - 8);
            pad_apply_long_mode(lbl_bottom, bcfg.style_bottom);
            lv_label_set_text(lbl_bottom, bcfg.label_bottom);
            lv_obj_align(lbl_bottom, LV_ALIGN_BOTTOM_MID, 0, bcfg.style_bottom.y_offset);
            lv_obj_set_style_text_align(lbl_bottom, pad_resolve_align(bcfg.style_bottom), 0);
            lv_obj_clear_flag(lbl_bottom, LV_OBJ_FLAG_CLICKABLE);
        }

        // Store runtime data
        tile.obj = obj;
        tile.label_top = lbl_top;
        tile.label_center = lbl_center;
        tile.label_bottom = lbl_bottom;
        tile.icon_img = icon_img;
        // icon_is_mono is set inside the icon block above; default false via clearTiles() memset
        tile.page = pageIndex;
        tile.col = bcfg.col;
        tile.row = bcfg.row;
        memcpy(&tile.action, &bcfg.action, sizeof(ButtonAction));
        memcpy(&tile.lp_action, &bcfg.lp_action, sizeof(ButtonAction));

        // Create MQTT-bound center label early so widgets can position it
#if HAS_MQTT
        if (binding_template_has_bindings(bcfg.label_center) && !tile.label_center && !bcfg.icon_id[0]) {
            tile.label_center = lv_label_create(obj);
            lv_obj_set_style_text_color(tile.label_center, pad_resolve_label_color(bcfg.style_center, fg), 0);
            lv_obj_set_style_text_font(tile.label_center, pad_resolve_font(bcfg.style_center, scale.font_large), 0);
            lv_obj_set_width(tile.label_center, r.w - 8);
            pad_apply_long_mode(tile.label_center, bcfg.style_center);
            lv_label_set_text(tile.label_center, "");
            lv_obj_align(tile.label_center, LV_ALIGN_CENTER, 0, bcfg.style_center.y_offset);
            lv_obj_set_style_text_align(tile.label_center, pad_resolve_align(bcfg.style_center), 0);
            lv_obj_clear_flag(tile.label_center, LV_OBJ_FLAG_CLICKABLE);
        }
#endif

        // Widget initialization
        tile.widget_type = nullptr;
        memset(&tile.widget_state, 0, sizeof(WidgetState));
        for (int wb = 0; wb < MAX_WIDGET_BINDINGS; wb++) tile.widget_binding[wb][0] = '\0';
        tile.widget_last[0] = '\0';
        if (bcfg.widget.type[0]) {
            const WidgetType* wt = widget_find(bcfg.widget.type);
            if (wt) {
                tile.widget_type = wt;
                memcpy(&tile.widget_cfg, &bcfg.widget, sizeof(WidgetConfig));
                // Widget data binding templates
                strlcpy(tile.widget_binding[0], bcfg.widget.data_binding[0], CONFIG_LABEL_MAX_LEN);
                strlcpy(tile.widget_binding[1], bcfg.widget.data_binding[1], CONFIG_LABEL_MAX_LEN);
                strlcpy(tile.widget_binding[2], bcfg.widget.data_binding[2], CONFIG_LABEL_MAX_LEN);
                if (wt->createUI) {
                    // Pass icon or center label — widget positions it above the bar
                    lv_obj_t* header_obj = tile.icon_img ? tile.icon_img : tile.label_center;
                    wt->createUI(obj, &tile.widget_cfg, &bcfg, &r, &scale, header_obj, &tile.widget_state);
                }
            }
        }

#if HAS_MQTT
        // Register template-based label bindings for labels containing [scheme:...]
        auto addTemplateBinding = [this](lv_obj_t* lbl, const char* label_text) {
            if (!lbl || !label_text || !label_text[0]) return;
            if (!binding_template_has_bindings(label_text)) return;
            if (bindingCount >= MAX_BINDINGS) return;
            RuntimeLabelBinding& rb = bindings[bindingCount];
            rb.label = lbl;
            strlcpy(rb.templ, label_text, sizeof(rb.templ));
            rb.last[0] = '\0';
            rb.active = true;
            bindingCount++;
            // Show placeholder until first resolve
            lv_label_set_text(lbl, "---");
        };
        addTemplateBinding(tile.label_top, bcfg.label_top);
        addTemplateBinding(tile.label_center, bcfg.label_center);
        addTemplateBinding(tile.label_bottom, bcfg.label_bottom);

        // Register color bindings for binding-based colors
        auto addColorBinding = [this](uint8_t ti, const char* templ, uint32_t def, uint8_t target) {
            if (!templ || !templ[0]) return;
            if (colorBindingCount >= MAX_COLOR_BINDINGS) return;
            RuntimeColorBinding& cb = colorBindings[colorBindingCount];
            cb.tileIndex = ti;
            strlcpy(cb.templ, templ, sizeof(cb.templ));
            cb.defaultColor = def;
            cb.lastApplied = def; // Already rendered with default
            cb.target = target;
            cb.active = true;
            cb.hasBindings = binding_template_has_bindings(templ);
            colorBindingCount++;
        };
        addColorBinding(i, bcfg.bg_color, bg_def, 0);
        addColorBinding(i, bcfg.fg_color, fg_def, 1);
        addColorBinding(i, bcfg.border_color, border_def, 2);

        // Register number bindings for border_width and corner_radius
        auto addNumberBinding = [this](uint8_t ti, const char* templ, lv_coord_t def, uint8_t target) {
            if (!templ || !templ[0]) return;
            if (numberBindingCount >= MAX_NUMBER_BINDINGS) return;
            RuntimeNumberBinding& nb = numberBindings[numberBindingCount];
            nb.tileIndex = ti;
            strlcpy(nb.templ, templ, sizeof(nb.templ));
            nb.defaultVal = def;
            nb.lastApplied = def;
            nb.target = target;
            nb.active = true;
            nb.hasBindings = binding_template_has_bindings(templ);
            numberBindingCount++;
        };
        addNumberBinding(i, bcfg.border_width, bw_def, 0);
        addNumberBinding(i, bcfg.corner_radius, cr_def, 1);
#endif

        // Event handlers — store tile index in user_data
        // Pack page index and tile index together for the callback
        uintptr_t user = ((uintptr_t)this);
        lv_obj_set_user_data(obj, (void*)user);

        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(obj, onTap, LV_EVENT_SHORT_CLICKED, &tiles[i]);
        lv_obj_add_event_cb(obj, onLongPress, LV_EVENT_LONG_PRESSED, &tiles[i]);

        // Register btn_state binding if configured
        if (bcfg.btn_state[0] && btnStateBindingCount < MAX_PAD_BUTTONS) {
            RuntimeBtnStateBinding& sb = btnStateBindings[btnStateBindingCount];
            sb.tileIndex = i;
            strlcpy(sb.templ, bcfg.btn_state, sizeof(sb.templ));
            sb.lastState = 0xFF; // uninitialized — force first apply
            sb.active = true;
            sb.hasBindings = binding_template_has_bindings(bcfg.btn_state);
            btnStateBindingCount++;
        }

#if HAS_IMAGE_FETCH
        // Request background image fetch if URL is configured
        tile.bg_image = nullptr;
        tile.image_slot = IMAGE_SLOT_INVALID;
        memset(&tile.img_dsc, 0, sizeof(tile.img_dsc));
        tile.owned_pixels = nullptr;
        tile.owned_pixels_size = 0;

        if (bcfg.bg_image_url[0]) {
            ImageScaleMode sm = bcfg.bg_image_letterbox ? IMAGE_SCALE_LETTERBOX : IMAGE_SCALE_COVER;
            tile.image_slot = image_fetch_request(
                bcfg.bg_image_url, bcfg.bg_image_user, bcfg.bg_image_password,
                r.w, r.h, bcfg.bg_image_interval_ms, sm);

            if (tile.image_slot != IMAGE_SLOT_INVALID) {
                // Create LVGL image widget as background (behind labels)
                tile.bg_image = lv_image_create(obj);
                lv_obj_set_size(tile.bg_image, r.w, r.h);
                lv_obj_set_align(tile.bg_image, LV_ALIGN_CENTER);
                lv_obj_clear_flag(tile.bg_image, LV_OBJ_FLAG_CLICKABLE);
                // Move to back so labels render on top
                lv_obj_move_to_index(tile.bg_image, 0);

                LOGD(TAG, "Tile %u: image slot %d for %.40s", i, tile.image_slot, bcfg.bg_image_url);
            }
        }
#endif

        // Tap overlay — semi-transparent sheet shown briefly on press.
        // Created last so it renders on top of all children (image bg, widgets, labels).
        // Color adapts to background luminance: dark overlay on light bg, light on dark.
        {
            bool is_light = perceived_luminance(bg_def) > TAP_LUMINANCE_THRESH;
            int16_t inset = TILE_PAD_PX + bw_def; // pad + border
            lv_obj_t* ov = lv_obj_create(obj);
            lv_obj_set_pos(ov, -inset, -inset);
            lv_obj_set_size(ov, r.w, r.h);
            lv_obj_set_style_bg_color(ov, is_light ? lv_color_black() : lv_color_white(), 0);
            lv_obj_set_style_bg_opa(ov, is_light ? TAP_OVERLAY_DARK_OPA : TAP_OVERLAY_LIGHT_OPA, 0);
            lv_obj_set_style_border_width(ov, 0, 0);
            lv_obj_set_style_radius(ov, 0, 0);
            lv_obj_set_style_pad_all(ov, 0, 0);
            lv_obj_clear_flag(ov, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
            lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
            tile.tap_overlay = ov;
        }

        tileCount++;
    }

#if HAS_MQTT
    // Register page-level background color binding (target=0xFF = page bg, tileIndex unused)
    if (pageBgTemplate[0] && binding_template_has_bindings(pageBgTemplate) && colorBindingCount < MAX_COLOR_BINDINGS) {
        RuntimeColorBinding& cb = colorBindings[colorBindingCount];
        cb.tileIndex = 0xFF; // sentinel: page background
        strlcpy(cb.templ, pageBgTemplate, sizeof(cb.templ));
        cb.defaultColor = pageBgDefault;
        cb.lastApplied = pageBgDefault;
        cb.target = 0; // bg
        cb.active = true;
        cb.hasBindings = true;
        colorBindingCount++;
    }
#endif

    free(cfg);
    tilesBuilt = true;

    LOGI(TAG, "Page %u: built %u tiles (%dx%d display)", pageIndex, tileCount, disp_w, disp_h);
}

// ============================================================================
// Event Handlers
// ============================================================================

// Tap flash: show overlay briefly, then hide after timeout
void PadScreen::tapFlashTimerCb(lv_timer_t* timer) {
    lv_obj_t* overlay = (lv_obj_t*)lv_timer_get_user_data(timer);
    if (overlay) {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_delete(timer);
}

static void do_tap_flash(ButtonTile* tile) {
    if (!tile->tap_overlay) return;
    lv_obj_remove_flag(tile->tap_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(PadScreen::tapFlashTimerCb, TAP_FLASH_DURATION_MS, tile->tap_overlay);
}

// Execute a typed action (dispatch on action.type)
static void execute_action(const ButtonAction& act, const char* label) {
    if (!act.type[0]) return; // No action

    if (strcmp(act.type, ACTION_TYPE_SCREEN) == 0) {
        if (act.screen_id[0]) {
            bool ok = false;
            display_manager_show_screen(act.screen_id, &ok);
            if (!ok) {
                LOGW(TAG, "%s nav failed: '%s'", label, act.screen_id);
            }
        }
    } else if (strcmp(act.type, ACTION_TYPE_BACK) == 0) {
        if (!display_manager_go_back()) {
            LOGW(TAG, "%s back: no previous screen", label);
        }
    } else if (strcmp(act.type, ACTION_TYPE_MQTT) == 0) {
#if HAS_MQTT
        if (act.mqtt_topic[0]) {
            bool ok = mqtt_manager.publish(act.mqtt_topic, act.mqtt_payload, false);
            LOGI(TAG, "%s mqtt: topic='%s' payload='%s' %s", label, act.mqtt_topic, act.mqtt_payload, ok ? "ok" : "FAIL");
        } else {
            LOGW(TAG, "%s mqtt: empty topic", label);
        }
#else
        LOGW(TAG, "%s mqtt: not compiled (HAS_MQTT=false)", label);
#endif
    } else {
        LOGW(TAG, "%s unknown action type: '%s'", label, act.type);
    }
}

// Publish HA event entity payload for a button press/hold
#if HAS_MQTT
static void publish_button_event(const ButtonTile* tile, const char* event_type) {
    if (!mqtt_manager.connected()) return;

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/event", mqtt_manager.baseTopic());

    StaticJsonDocument<256> doc;
    doc["event_type"] = event_type;
    doc["page"] = tile->page;
    doc["col"] = tile->col;
    doc["row"] = tile->row;

    // Pick the most prominent label text
    const char* label = "";
    if (tile->label_center) label = lv_label_get_text(tile->label_center);
    if ((!label || !label[0]) && tile->label_top) label = lv_label_get_text(tile->label_top);
    if ((!label || !label[0]) && tile->label_bottom) label = lv_label_get_text(tile->label_bottom);
    if (label && label[0]) doc["label"] = label;

    mqtt_manager.publishJson(topic, doc, false);
}
#endif

void PadScreen::onTap(lv_event_t* e) {
    ButtonTile* tile = (ButtonTile*)lv_event_get_user_data(e);
    if (!tile || !tile->obj) return;

    // Tap flash
    do_tap_flash(tile);

    execute_action(tile->action, "Tap");

#if HAS_MQTT
    publish_button_event(tile, "press");
#endif
}

void PadScreen::onLongPress(lv_event_t* e) {
    ButtonTile* tile = (ButtonTile*)lv_event_get_user_data(e);
    if (!tile || !tile->obj) return;

    // Tap flash
    do_tap_flash(tile);

    execute_action(tile->lp_action, "LP");

#if HAS_MQTT
    publish_button_event(tile, "hold");
#endif
}

// ============================================================================
// MQTT Label Binding Polling
// ============================================================================

void PadScreen::pollMqttBindings() {
#if HAS_MQTT
    char resolved[BINDING_TEMPLATE_MAX_LEN];

    for (uint16_t i = 0; i < bindingCount; i++) {
        RuntimeLabelBinding& rb = bindings[i];
        if (!rb.active || !rb.label) continue;

        // Resolve template — replaces [scheme:...] tokens with live values
        binding_template_resolve(rb.templ, resolved, sizeof(resolved));

        // Only update LVGL label if text changed
        if (strcmp(resolved, rb.last) == 0) continue;
        strlcpy(rb.last, resolved, sizeof(rb.last));
        lv_label_set_text(rb.label, resolved);
    }

    // Update widget tiles from their data binding template(s)
    // Multi-ring widgets use binding_2/3 for additional slots.
    // Preserve empty intermediate slots so widgets can distinguish
    // "ring 3 only" from promoted ring 2 behavior.
    char widget_resolved[BINDING_TEMPLATE_MAX_LEN];
    char widget_combined[BINDING_TEMPLATE_MAX_LEN * 3 + 4];

    for (uint8_t i = 0; i < tileCount; i++) {
        ButtonTile& tile = tiles[i];
        if (!tile.widget_type || !tile.widget_type->update) continue;
        if (!tile.widget_binding[0][0]) continue;

        // Resolve primary binding
        binding_template_resolve(tile.widget_binding[0], widget_resolved, sizeof(widget_resolved));

        // Build combined string with empty intermediate slots preserved,
        // e.g. "val1\t\tval3" when only binding_3 is set.
        size_t off = strlcpy(widget_combined, widget_resolved, sizeof(widget_combined));
        int max_slot = 0;
        for (int wb = MAX_WIDGET_BINDINGS - 1; wb >= 1; wb--) {
            if (tile.widget_binding[wb][0]) {
                max_slot = wb;
                break;
            }
        }
        for (int wb = 1; wb <= max_slot; wb++) {
            if (off < sizeof(widget_combined) - 1) {
                widget_combined[off++] = '\t';
                widget_combined[off] = '\0';
            }
            if (!tile.widget_binding[wb][0]) continue;
            binding_template_resolve(tile.widget_binding[wb], widget_resolved, sizeof(widget_resolved));
            off += strlcpy(widget_combined + off, widget_resolved, sizeof(widget_combined) - off);
            if (off >= sizeof(widget_combined)) {
                off = sizeof(widget_combined) - 1;
                widget_combined[off] = '\0';
            }
        }

        // Only update widget if any resolved value changed
        if (strcmp(widget_combined, tile.widget_last) != 0) {
            strlcpy(tile.widget_last, widget_combined, sizeof(tile.widget_last));
            tile.widget_type->update(tile.obj, &tile.widget_cfg, &tile.widget_state, widget_combined);
        }

        // Always call tick (if implemented) for time-driven widgets
        if (tile.widget_type->tick) {
            tile.widget_type->tick(tile.obj, &tile.widget_cfg, &tile.widget_state);
        }
    }
#endif
}

// ============================================================================
// Color Binding Polling
// ============================================================================

void PadScreen::pollColorBindings() {
#if HAS_MQTT
    if (colorBindingCount == 0) return;

    char resolved[BINDING_TEMPLATE_MAX_LEN];

    for (uint16_t i = 0; i < colorBindingCount; i++) {
        RuntimeColorBinding& cb = colorBindings[i];
        if (!cb.active) continue;

        uint32_t color = cb.defaultColor;

        if (cb.hasBindings) {
            // Resolve binding template to get a color string
            binding_template_resolve(cb.templ, resolved, sizeof(resolved));
            uint32_t parsed;
            if (parse_hex_color(resolved, &parsed)) {
                color = parsed;
            }
            // else: unresolved / error — keep default
        } else {
            // Static color string — parse once then deactivate
            uint32_t parsed;
            if (parse_hex_color(cb.templ, &parsed)) {
                color = parsed;
            }
            cb.active = false; // static value won't change; stop polling
        }

        // Only update LVGL if color changed
        if (color == cb.lastApplied) continue;
        cb.lastApplied = color;

        // Page background (sentinel tileIndex=0xFF)
        if (cb.tileIndex == 0xFF) {
            if (screen) lv_obj_set_style_bg_color(screen, rgb_to_lv(color), 0);
            continue;
        }

        uint8_t ti = cb.tileIndex;
        if (ti >= tileCount) continue;
        ButtonTile& tile = tiles[ti];

        switch (cb.target) {
        case 0: // bg
            lv_obj_set_style_bg_color(tile.obj, rgb_to_lv(color), 0);
            // Update overlay color to match new background luminance
            if (tile.tap_overlay) {
                bool is_light = perceived_luminance(color) > TAP_LUMINANCE_THRESH;
                lv_obj_set_style_bg_color(tile.tap_overlay, is_light ? lv_color_black() : lv_color_white(), 0);
                lv_obj_set_style_bg_opa(tile.tap_overlay, is_light ? TAP_OVERLAY_DARK_OPA : TAP_OVERLAY_LIGHT_OPA, 0);
            }
            break;
        case 1: // fg (labels + mono icon recolor)
            if (tile.label_top) lv_obj_set_style_text_color(tile.label_top, rgb_to_lv(color), 0);
            if (tile.label_center) lv_obj_set_style_text_color(tile.label_center, rgb_to_lv(color), 0);
            if (tile.label_bottom) lv_obj_set_style_text_color(tile.label_bottom, rgb_to_lv(color), 0);
            if (tile.icon_img && tile.icon_is_mono) {
                lv_obj_set_style_image_recolor(tile.icon_img, rgb_to_lv(color), 0);
            }
            break;
        case 2: // border
            lv_obj_set_style_border_color(tile.obj, rgb_to_lv(color), 0);
            break;
        }
    }
#endif
}

// ============================================================================
// Number Binding Polling (border_width, corner_radius)
// ============================================================================

void PadScreen::pollNumberBindings() {
#if HAS_MQTT
    char resolved[BINDING_TEMPLATE_MAX_LEN];
    for (uint16_t i = 0; i < numberBindingCount; i++) {
        RuntimeNumberBinding& nb = numberBindings[i];
        if (!nb.active) continue;

        lv_coord_t val = nb.defaultVal;
        if (nb.hasBindings) {
            binding_template_resolve(nb.templ, resolved, sizeof(resolved));
            char* end = nullptr;
            float fv = strtof(resolved, &end);
            if (end != resolved) val = (lv_coord_t)fv;
        } else {
            char* end = nullptr;
            float fv = strtof(nb.templ, &end);
            if (end != nb.templ) val = (lv_coord_t)fv;
            nb.active = false; // static value won't change; stop polling
        }

        if (val == nb.lastApplied) continue;
        nb.lastApplied = val;

        uint8_t ti = nb.tileIndex;
        if (ti >= tileCount) continue;
        ButtonTile& tile = tiles[ti];

        switch (nb.target) {
        case 0: // border_width
            lv_obj_set_style_border_width(tile.obj, val, 0);
            break;
        case 1: // corner_radius
            lv_obj_set_style_radius(tile.obj, val, 0);
            break;
        }
    }
#endif
}

// ============================================================================
// Button State Binding Polling
// ============================================================================

static BtnState parse_btn_state(const char* resolved) {
    if (!resolved || !resolved[0]) return BTN_STATE_ENABLED;
    if (strcmp(resolved, "disabled") == 0) return BTN_STATE_DISABLED;
    if (strcmp(resolved, "hidden") == 0)   return BTN_STATE_HIDDEN;
    // "enabled", "---" (unresolved), "ERR:..." → default to enabled
    return BTN_STATE_ENABLED;
}

static void apply_btn_state(ButtonTile& tile, BtnState state) {
    if (!tile.obj) return;
    switch (state) {
    case BTN_STATE_ENABLED:
        lv_obj_clear_flag(tile.obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(tile.obj, LV_STATE_DISABLED);
        break;
    case BTN_STATE_DISABLED:
        lv_obj_clear_flag(tile.obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(tile.obj, LV_STATE_DISABLED);
        break;
    case BTN_STATE_HIDDEN:
        lv_obj_add_flag(tile.obj, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

void PadScreen::pollBtnStateBindings() {
    if (btnStateBindingCount == 0) return;

    char resolved[BINDING_TEMPLATE_MAX_LEN];

    for (uint16_t i = 0; i < btnStateBindingCount; i++) {
        RuntimeBtnStateBinding& sb = btnStateBindings[i];
        if (!sb.active) continue;

        BtnState state;

        if (sb.hasBindings) {
            binding_template_resolve(sb.templ, resolved, sizeof(resolved));
            state = parse_btn_state(resolved);
        } else {
            // Static value — parse once then deactivate
            state = parse_btn_state(sb.templ);
            sb.active = false;
        }

        if ((uint8_t)state == sb.lastState) continue;
        sb.lastState = (uint8_t)state;

        uint8_t ti = sb.tileIndex;
        if (ti >= tileCount) continue;
        ButtonTile& tile = tiles[ti];

        apply_btn_state(tile, state);

#if HAS_IMAGE_FETCH
        // Pause/resume image fetch for hidden tiles
        if (tile.image_slot != IMAGE_SLOT_INVALID) {
            if (state == BTN_STATE_HIDDEN) {
                image_fetch_pause_slot(tile.image_slot);
            } else {
                image_fetch_resume_slot(tile.image_slot);
            }
        }
#endif
    }
}

// ============================================================================
// Image Frame Polling
// ============================================================================

#if HAS_IMAGE_FETCH
void PadScreen::pollImageFrames() {
    for (uint8_t i = 0; i < tileCount; i++) {
        ButtonTile& tile = tiles[i];
        if (tile.image_slot == IMAGE_SLOT_INVALID || !tile.bg_image) continue;

        if (!image_fetch_has_new_frame(tile.image_slot)) continue;

        uint16_t fw = 0, fh = 0;
        const uint16_t* pixels = image_fetch_get_frame(tile.image_slot, &fw, &fh);
        if (!pixels || fw == 0 || fh == 0) continue;

        // Copy frame into tile-owned buffer so LVGL has a stable pointer
        // independent of the fetch task's double-buffer rotation.
        size_t needed = (size_t)fw * fh * 2;
        if (tile.owned_pixels_size < needed) {
            if (tile.owned_pixels) heap_caps_free(tile.owned_pixels);
            tile.owned_pixels = (uint16_t*)heap_caps_malloc(needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            tile.owned_pixels_size = tile.owned_pixels ? needed : 0;
        }
        if (!tile.owned_pixels) {
            image_fetch_ack_frame(tile.image_slot);
            continue;
        }
        memcpy(tile.owned_pixels, pixels, needed);
        image_fetch_ack_frame(tile.image_slot);

        // Build LVGL image descriptor pointing to our owned copy
        lv_image_dsc_t& dsc = tile.img_dsc;
        dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        dsc.header.flags = 0;
        dsc.header.w = fw;
        dsc.header.h = fh;
        dsc.header.stride = fw * 2;
        dsc.data_size = (uint32_t)needed;
        dsc.data = (const uint8_t*)tile.owned_pixels;

        lv_image_set_src(tile.bg_image, &dsc);
    }
}
#endif

void PadScreen::onSwipe(lv_event_t* e) {
    PadScreen* self = (PadScreen*)lv_event_get_user_data(e);
    if (!self) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

    // Swipe left/right to move between pad pages
    int next_page = -1;
    if (dir == LV_DIR_LEFT && self->pageIndex < MAX_PAD_PAGES - 1) {
        next_page = self->pageIndex + 1;
    } else if (dir == LV_DIR_RIGHT && self->pageIndex > 0) {
        next_page = self->pageIndex - 1;
    }

    if (next_page >= 0) {
        char screen_id[16];
        snprintf(screen_id, sizeof(screen_id), "pad_%d", next_page);
        bool ok = false;
        display_manager_show_screen(screen_id, &ok);
        if (ok) {
            LOGD(TAG, "Swipe to pad_%d", next_page);
        }
    }
}
