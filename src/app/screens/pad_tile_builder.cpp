#include "pad_screen.h"
#include "../display_manager.h"
#include "../icon_store.h"
#include "../log_manager.h"
#include "../device_class.h"
#include <esp_heap_caps.h>
#include <string.h>

// TAG, perceived_luminance(), rgb_to_lv(), and tile/tap-flash constants are
// defined in pad_screen.cpp which is #included before this file in screens.cpp.

// ============================================================================
// Tile Management
// ============================================================================

void PadScreen::clearTiles() {
    clearPadActionOverlay();
    free(padActions);
    padActions = nullptr;
    padActionCount = 0;
    if (!tiles) { tileCount = 0; tilesBuilt = false; return; }
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
        // Pixel data is owned by image_fetch's lvgl_buf (zero-copy);
        // image_fetch_cancel() above releases it.
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

    // Ensure lazy arrays are allocated before building
    if (!allocateArrays()) {
        LOGE(TAG, "Pad %u: OOM for binding arrays", pageIndex);
        tilesBuilt = true; // Mark built (empty) to avoid retrying every frame
        return;
    }

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
        tilesBuilt = true; // Mark built (empty) to avoid retrying every frame
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

    padActionCount = cfg->pad_action_count;
    if (padActionCount > 0) {
        if (allocatePadActions()) {
            memcpy(padActions, cfg->pad_actions,
                   padActionCount * sizeof(ButtonAction));
        } else {
            padActionCount = 0;
        }
    }

    // Only grid layout supported in v0
    if (strcmp(cfg->layout, "grid") != 0) {
        LOGW(TAG, "Page %u: unsupported layout '%s', skipping", pageIndex, cfg->layout);
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

        // Content padding — plain px inset for labels/icon/widget. Cascades
        // button → device defaults → firmware default "4" (the legacy fixed inset).
        long cp_raw = strtol(bcfg.content_pad, nullptr, 10);
        if (cp_raw < 0) cp_raw = 0; else if (cp_raw > 50) cp_raw = 50;
        const int16_t pad = (int16_t)cp_raw;
        const int16_t lbl_w = (r.w > 2 * pad) ? (int16_t)(r.w - 2 * pad) : 0;
        lv_obj_set_style_pad_all(obj, pad, 0);

        lv_color_t fg = rgb_to_lv(fg_def);
        lv_obj_set_style_text_color(obj, fg, 0);

        // Top label
        lv_obj_t* lbl_top = nullptr;
        if (bcfg.label_top[0]) {
            lbl_top = lv_label_create(obj);
            lv_obj_set_style_text_color(lbl_top, pad_resolve_label_color(bcfg.style_top, fg), 0);
            lv_obj_set_style_text_font(lbl_top, pad_resolve_font(bcfg.style_top, scale.font_small), 0);
            lv_obj_set_width(lbl_top, lbl_w);
            pad_apply_long_mode(lbl_top, bcfg.style_top);
            lv_label_set_text(lbl_top, bcfg.label_top);
            pad_apply_font_upscale(lbl_top, bcfg.style_top, PAD_LABEL_ANCHOR_TOP);
            lv_obj_align(lbl_top, LV_ALIGN_TOP_MID,
                         ui_ofs_x + bcfg.style_top.x_offset,
                         bcfg.style_top.y_offset + ui_ofs_y);
            lv_obj_set_style_text_align(lbl_top, pad_resolve_align(bcfg.style_top), 0);
            lv_obj_clear_flag(lbl_top, LV_OBJ_FLAG_CLICKABLE);
        }

        // Center label
        lv_obj_t* lbl_center = nullptr;
        if (bcfg.label_center[0]) {
            lbl_center = lv_label_create(obj);
            lv_obj_set_style_text_color(lbl_center, pad_resolve_label_color(bcfg.style_center, fg), 0);
            lv_obj_set_style_text_font(lbl_center, pad_resolve_font(bcfg.style_center, scale.font_large), 0);
            lv_obj_set_width(lbl_center, lbl_w);
            pad_apply_long_mode(lbl_center, bcfg.style_center);
            lv_label_set_text(lbl_center, bcfg.label_center);
            pad_apply_font_upscale(lbl_center, bcfg.style_center, PAD_LABEL_ANCHOR_CENTER);
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
                lv_obj_clear_flag(icon_img, LV_OBJ_FLAG_CLICKABLE);
                if (ref.kind == ICON_KIND_MONO) {
                    lv_obj_set_style_image_recolor(icon_img, fg, 0);
                    lv_obj_set_style_image_recolor_opa(icon_img, LV_OPA_COVER, 0);
                    tile.icon_is_mono = true;
                }
            }
        }

        // Position icon + center label based on icon_position mode
        // Widgets override layout, so clamp to ABOVE when a widget is active
        const uint8_t effective_icon_pos = bcfg.widget.type[0] ? ICON_POS_ABOVE : bcfg.icon_position;
        if (icon_img && lbl_center) {
            // Force layout to get actual dimensions
            lv_obj_update_layout(icon_img);
            lv_obj_update_layout(lbl_center);
            const int16_t icon_h = (int16_t)lv_obj_get_height(icon_img);
            const int16_t icon_w = (int16_t)lv_obj_get_width(icon_img);
            const int16_t lbl_h = (int16_t)lv_obj_get_height(lbl_center);

            const lv_font_t* top_font = pad_resolve_font(bcfg.style_top, scale.font_small);
            const lv_font_t* bot_font = pad_resolve_font(bcfg.style_bottom, scale.font_small);
            const int16_t top_h = bcfg.label_top[0] ? lv_font_get_line_height(top_font) : 0;
            const int16_t bot_h = bcfg.label_bottom[0] ? lv_font_get_line_height(bot_font) : 0;
            const int16_t label_bias = (top_h - bot_h) / 2;

            if (effective_icon_pos == ICON_POS_LEFT) {
                // Horizontal row: icon left, label right, vertically centered
                const int16_t inset_x = pad;
                const int16_t gap = 4;
                // Shrink label so icon + gap + label fits inside the button
                const int16_t lbl_max_w = r.w - 2 * inset_x - icon_w - gap;
                if (lbl_max_w > 0) {
                    lv_obj_set_width(lbl_center, lbl_max_w);
                    lv_obj_set_style_text_align(lbl_center, LV_TEXT_ALIGN_LEFT, 0);
                }
                // Position relative to button center (LV_ALIGN_CENTER offset 0 = middle)
                const int16_t icon_cx = -r.w / 2 + inset_x + icon_w / 2;
                const int16_t lbl_cx  = -r.w / 2 + inset_x + icon_w + gap
                                        + (lbl_max_w > 0 ? lbl_max_w / 2 : 0);
                lv_obj_align(icon_img, LV_ALIGN_CENTER,
                             icon_cx + ui_ofs_x,
                             label_bias + ui_ofs_y);
                lv_obj_align(lbl_center, LV_ALIGN_CENTER,
                             lbl_cx + ui_ofs_x + bcfg.style_center.x_offset,
                             label_bias + bcfg.style_center.y_offset + ui_ofs_y);
            } else if (effective_icon_pos == ICON_POS_CENTER) {
                // Icon centered, label stays at its default position (no displacement)
                lv_obj_align(icon_img, LV_ALIGN_CENTER, ui_ofs_x, label_bias + ui_ofs_y);
                // lbl_center is already at LV_ALIGN_CENTER from initial creation above
            } else {
                // ICON_POS_ABOVE (default): icon above label, both centered
                const int16_t gap = 4;
                const int16_t stack_h = icon_h + gap + lbl_h;
                const int16_t stack_top_ofs = -stack_h / 2 + label_bias;
                lv_obj_align(icon_img, LV_ALIGN_CENTER,
                             ui_ofs_x,
                             stack_top_ofs + icon_h / 2 + ui_ofs_y);
                lv_obj_align(lbl_center, LV_ALIGN_CENTER,
                             ui_ofs_x + bcfg.style_center.x_offset,
                             stack_top_ofs + icon_h + gap + lbl_h / 2
                                 + bcfg.style_center.y_offset + ui_ofs_y);
            }
        } else if (icon_img) {
            // Icon only (no center label) — center in available space
            const lv_font_t* top_font = pad_resolve_font(bcfg.style_top, scale.font_small);
            const lv_font_t* bot_font = pad_resolve_font(bcfg.style_bottom, scale.font_small);
            const int16_t top_h = bcfg.label_top[0] ? lv_font_get_line_height(top_font) : 0;
            const int16_t bot_h = bcfg.label_bottom[0] ? lv_font_get_line_height(bot_font) : 0;
            const int16_t y_ofs = (top_h - bot_h) / 2;
            lv_obj_align(icon_img, LV_ALIGN_CENTER, ui_ofs_x, y_ofs + ui_ofs_y);
        }
        // else: lbl_center only — already positioned at LV_ALIGN_CENTER above

        // Bottom label
        lv_obj_t* lbl_bottom = nullptr;
        if (bcfg.label_bottom[0]) {
            lbl_bottom = lv_label_create(obj);
            lv_obj_set_style_text_color(lbl_bottom, pad_resolve_label_color(bcfg.style_bottom, fg), 0);
            lv_obj_set_style_text_font(lbl_bottom, pad_resolve_font(bcfg.style_bottom, scale.font_small), 0);
            lv_obj_set_width(lbl_bottom, lbl_w);
            pad_apply_long_mode(lbl_bottom, bcfg.style_bottom);
            lv_label_set_text(lbl_bottom, bcfg.label_bottom);
            pad_apply_font_upscale(lbl_bottom, bcfg.style_bottom, PAD_LABEL_ANCHOR_BOTTOM);
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
        tile.pad_bindings = pageBindings;
        tile.pad_binding_count = pageBindingCount;
        tile.col = bcfg.col;
        tile.row = bcfg.row;
        tile.action_count = bcfg.action_count;
        memcpy(tile.actions, bcfg.actions, bcfg.action_count * sizeof(ButtonAction));
        tile.lp_action_count = bcfg.lp_action_count;
        memcpy(tile.lp_actions, bcfg.lp_actions, bcfg.lp_action_count * sizeof(ButtonAction));
        tile.confirm = bcfg.confirm;
        strlcpy(tile.confirm_text, bcfg.confirm_text, sizeof(tile.confirm_text));

        // Create MQTT-bound center label early so widgets can position it
#if HAS_MQTT
        if (binding_template_has_bindings(bcfg.label_center) && !tile.label_center) {
            tile.label_center = lv_label_create(obj);
            lv_obj_set_style_text_color(tile.label_center, pad_resolve_label_color(bcfg.style_center, fg), 0);
            lv_obj_set_style_text_font(tile.label_center, pad_resolve_font(bcfg.style_center, scale.font_large), 0);
            lv_obj_set_width(tile.label_center, lbl_w);
            pad_apply_long_mode(tile.label_center, bcfg.style_center);
            lv_label_set_text(tile.label_center, "");
            pad_apply_font_upscale(tile.label_center, bcfg.style_center, PAD_LABEL_ANCHOR_CENTER);
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
#if HAS_MQTT
                // Expand [pad:] tokens in widget data bindings so stream-based
                // widgets (e.g. sparkline) look up their data stream by the same
                // expanded key that data_stream_rebuild() registered it under.
                // Without this, [pad:name] data bindings never match a stream.
                for (int wb = 0; wb < MAX_WIDGET_BINDINGS; wb++) {
                    char expanded[CONFIG_LABEL_MAX_LEN];
                    if (pad_binding_expand(cfg, tile.widget_cfg.data_binding[wb],
                                           expanded, sizeof(expanded))) {
                        strlcpy(tile.widget_cfg.data_binding[wb], expanded, CONFIG_LABEL_MAX_LEN);
                    }
                }
#endif
                // Widget data binding templates
                for (int wb = 0; wb < MAX_WIDGET_BINDINGS; wb++) {
                    strlcpy(tile.widget_binding[wb], bcfg.widget.data_binding[wb], CONFIG_LABEL_MAX_LEN);
                }
                if (wt->createUI) {
                    wt->createUI(obj, &tile.widget_cfg, &bcfg, &r, &scale,
                                 tile.icon_img, tile.label_center, &tile.widget_state);
                }
            }
        }

#if HAS_MQTT
        // Register template-based label bindings for labels containing [scheme:...]
        auto addTemplateBinding = [this](lv_obj_t* lbl, const char* label_text,
                                         const LabelStyle& style,
                                         PadLabelAnchorY anchorY) {
            if (!lbl || !label_text || !label_text[0]) return;
            if (!binding_template_has_bindings(label_text)) return;
            if (bindingCount >= MAX_BINDINGS) return;
            RuntimeLabelBinding& rb = bindings[bindingCount];
            rb.label = lbl;
            strlcpy(rb.templ, label_text, sizeof(rb.templ));
            rb.last[0] = '\0';
            rb.style = style;
            rb.anchorY = (uint8_t)anchorY;
            rb.active = true;
            bindingCount++;
            // Show placeholder until first resolve
            lv_label_set_text(lbl, "---");
            pad_apply_font_upscale(lbl, style, anchorY);
        };
        addTemplateBinding(tile.label_top, bcfg.label_top, bcfg.style_top, PAD_LABEL_ANCHOR_TOP);
        addTemplateBinding(tile.label_center, bcfg.label_center, bcfg.style_center, PAD_LABEL_ANCHOR_CENTER);
        addTemplateBinding(tile.label_bottom, bcfg.label_bottom, bcfg.style_bottom, PAD_LABEL_ANCHOR_BOTTOM);

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

        // Per-label text colors. Only for normal button tiles (widget tiles manage
        // their own labels, which may be null). A per-label color — static or
        // binding — sets an override bit so the fg (target=1) poll does not clobber
        // the label's text color. Binding-valued colors are additionally registered
        // as runtime color bindings (targets 3/4/5) resolved live each poll cycle.
        if (!bcfg.widget.type[0]) {
            if (bcfg.style_top.color    & 0x01000000) tile.labelColorOverride |= 0x01;
            if (bcfg.style_center.color & 0x01000000) tile.labelColorOverride |= 0x02;
            if (bcfg.style_bottom.color & 0x01000000) tile.labelColorOverride |= 0x04;
            if (bcfg.label_top_color_bind[0]) {
                addColorBinding(i, bcfg.label_top_color_bind, fg_def, 3);
                tile.labelColorOverride |= 0x01;
            }
            if (bcfg.label_center_color_bind[0]) {
                addColorBinding(i, bcfg.label_center_color_bind, fg_def, 4);
                tile.labelColorOverride |= 0x02;
            }
            if (bcfg.label_bottom_color_bind[0]) {
                addColorBinding(i, bcfg.label_bottom_color_bind, fg_def, 5);
                tile.labelColorOverride |= 0x04;
            }
        }

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
            int16_t inset = pad + bw_def; // pad + border
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

    if (padActionCount > 0) {
        padActionOverlay = lv_obj_create(container);
        lv_obj_set_pos(padActionOverlay, 0, 0);
        lv_obj_set_size(padActionOverlay, disp_w, disp_h);
        lv_obj_set_style_bg_opa(padActionOverlay, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(padActionOverlay, 0, 0);
        lv_obj_set_style_pad_all(padActionOverlay, 0, 0);
        lv_obj_clear_flag(padActionOverlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(padActionOverlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(padActionOverlay, onPadActionTap,
                            LV_EVENT_SHORT_CLICKED, this);
        swipe_actions_register(padActionOverlay);
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

    // Detect device-class engine-hold consumers: any registered device class
    // that declares a pad_hold_scheme (e.g. "[shutter:") whose binding token
    // appears on this pad keeps its hardware engine running while the pad is
    // visible. Generic — no device class is named here.
    //
    // Only bindings (label/color/number/btn-state/widget) count: they resolve
    // every poll cycle and would render "---" without the engine running.
    // Device-class *actions* (tap/long-press) are intentionally NOT scanned —
    // they fire on demand and their handlers acquire the engine themselves, so
    // a nav pad that merely hosts a "start session" button should not keep the
    // engine active while idle (padHasScheme() only walks binding arrays).
    padHoldMask = 0;
    for (unsigned c = 0; c < device_class_count(); c++) {
        const DeviceClass* dc = device_class_get(c);
        if (!dc || !dc->pad_hold_scheme || !dc->pad_hold_acquire) continue;
        if (padHasScheme(dc->pad_hold_scheme)) {
            padHoldMask |= (uint8_t)(1u << c);
            LOGI(TAG, "Page %u: device-class '%s' consumer detected — will hold engine while visible",
                 pageIndex, dc->name);
        }
    }

    tilesBuilt = true;

    LOGI(TAG, "Page %u: built %u tiles (%dx%d display)", pageIndex, tileCount, disp_w, disp_h);
}
