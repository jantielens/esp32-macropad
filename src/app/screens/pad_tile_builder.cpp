#include "pad_screen.h"
#include "../display_manager.h"
#include "../icon_store.h"
#include "../log_manager.h"
#include <esp_heap_caps.h>
#include <string.h>

// TAG, perceived_luminance(), rgb_to_lv(), and tile/tap-flash constants are
// defined in pad_screen.cpp which is #included before this file in screens.cpp.

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

    // Allocate PadConfig in PSRAM (temporary — freed at end of this function)
    PadConfig* cfg = nullptr;
    if (psramFound()) {
        cfg = (PadConfig*)heap_caps_malloc(sizeof(PadConfig), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!cfg) {
        cfg = (PadConfig*)malloc(sizeof(PadConfig));
    }
    if (!cfg) {
        LOGE(TAG, "OOM for PadConfig");
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
        const int16_t ui_ofs_x = bcfg.ui_offset_x;
        const int16_t ui_ofs_y = bcfg.ui_offset_y;

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
            lv_obj_align(lbl_top, LV_ALIGN_TOP_MID,
                         ui_ofs_x + bcfg.style_top.x_offset,
                         bcfg.style_top.y_offset + ui_ofs_y);
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
            lv_obj_align(lbl_center, LV_ALIGN_CENTER,
                         ui_ofs_x + bcfg.style_center.x_offset,
                         bcfg.style_center.y_offset + ui_ofs_y);
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
                lv_obj_align(icon_img, LV_ALIGN_CENTER, ui_ofs_x, y_ofs + ui_ofs_y);

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
            lv_obj_align(lbl_bottom, LV_ALIGN_BOTTOM_MID,
                         ui_ofs_x + bcfg.style_bottom.x_offset,
                         bcfg.style_bottom.y_offset + ui_ofs_y);
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
            lv_obj_align(tile.label_center, LV_ALIGN_CENTER,
                         ui_ofs_x + bcfg.style_center.x_offset,
                         bcfg.style_center.y_offset + ui_ofs_y);
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
                for (int wb = 0; wb < MAX_WIDGET_BINDINGS; wb++) {
                    strlcpy(tile.widget_binding[wb], bcfg.widget.data_binding[wb], CONFIG_LABEL_MAX_LEN);
                }
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
