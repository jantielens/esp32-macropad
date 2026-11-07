#include "pad_screen.h"
#include "../display_manager.h"
#include "../icon_store.h"
#include "../log_manager.h"
#include "../device_class.h"
#include "../button_shadow_color.h"
#include "../button_content_lvgl.h"
#include "../fill_style_runtime.h"
#include "../widgets/option_selector_widget.h"
#if HAS_TOUCH
#include "../touch_manager.h"
#include "../widgets/slider_widget.h"
#endif
#include <esp_heap_caps.h>
#include <string.h>

static bool button_overlay_uses_fade(const char* widget_type) {
    return !DISPLAY_DISABLE_ANIMATIONS &&
        (strcmp(widget_type, "rocker") == 0 ||
         strcmp(widget_type, "numericrocker") == 0 ||
         strcmp(widget_type, "gamepad_button") == 0 ||
         strcmp(widget_type, "option_selector") == 0);
}

static bool button_shadow_enabled(const PadConfig& cfg, const ScreenButtonConfig& button) {
    bool enabled = cfg.shadow.enabled;
    if (cfg.button_shadow != BUTTON_SHADOW_INHERIT)
        enabled = cfg.button_shadow == BUTTON_SHADOW_ENABLED;
    if (button.button_shadow != BUTTON_SHADOW_INHERIT)
        enabled = button.button_shadow == BUTTON_SHADOW_ENABLED;
    return enabled;
}

static PadGridLayoutSpace pad_layout_space(const PadLayoutSettings& layout) {
    PadGridLayoutSpace space = {
        layout.inset_left_px, layout.inset_right_px,
        layout.inset_top_px, layout.inset_bottom_px,
        layout.spacing_px, layout.spacing_px,
        layout.pixel_shift_distance_px,
    };
    return space;
}

static uint16_t capped_icon_scale(const ScreenButtonConfig& button,
                                  const PadRect& rect, int16_t padding,
                                  const IconRef& icon) {
    const uint32_t source_width = icon.dsc->header.w;
    const uint32_t source_height = icon.dsc->header.h;
    if (source_width != ICON_MAX_DIMENSION && source_height != ICON_MAX_DIMENSION) {
        return LV_SCALE_NONE;
    }

    const int16_t border_width = (int16_t)strtol(button.border_width, nullptr, 10);
    int32_t target_width = rect.w - 2 * padding - 2 * border_width;
    int32_t target_height = rect.h - 2 * padding - 2 * border_width;

    if (button.icon_scale_pct > 0) {
        const uint8_t percent = min<uint8_t>(button.icon_scale_pct, 250);
        target_width = target_width * percent / 100;
        target_height = target_height * percent / 100;
    }

    const uint32_t target_size = max<int32_t>(1, min(target_width, target_height));
    const uint32_t source_size = max(source_width, source_height);
    if (target_size <= source_size) return LV_SCALE_NONE;

    const uint32_t image_scale = target_size * LV_SCALE_NONE / source_size;
    return (uint16_t)min<uint32_t>(image_scale, UINT16_MAX);
}

// TAG, perceived_luminance(), rgb_to_lv(), and tile/tap-flash constants are
// defined in pad_screen.cpp which is #included before this file in screens.cpp.

static lv_obj_t* create_content_label(lv_obj_t* parent, const char* text, const LabelStyle& style,
                                      const lv_font_t* fallback, lv_color_t foreground,
                                      PadFontCache& fonts, PadLabelAnchorY anchor) {
    if (!text[0]) return nullptr;
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, pad_resolve_label_color(style, foreground), 0);
    lv_obj_set_style_text_font(label, pad_resolve_font(style, fallback, &fonts), 0);
    pad_apply_long_mode(label, style);
    lv_label_set_text(label, text);
    pad_apply_font_upscale(label, style, anchor);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

// ============================================================================
// Tile Management
// ============================================================================

void PadScreen::clearTiles() {
    if (configError) lv_obj_delete(configError);
    configError = nullptr;
    if (navigationFocus) lv_obj_delete(navigationFocus);
    navigationFocus = nullptr;
    destroyOverlayInputShield();
    #if HAS_TOUCH
    if (tileCount && screen && screen == lv_screen_active()) touch_manager_cancel_physical_input();
    #endif
    clearPadActionOverlay();
    free(padActions);
    padActions = nullptr;
    padActionCount = 0;
    if (!tiles) {
        fontCache.clear();
        tileCount = 0;
            mainButtonCount = 0;
        tilesBuilt = false;
        icon_store_collect_retired(pageIndex);
        return;
    }
    for (uint8_t i = 0; i < tileCount; i++) {
        // Destroy widget state before LVGL objects are deleted
        if (tiles[i].widget_type && tiles[i].widget_type->destroyUI) {
            tiles[i].widget_type->destroyUI(&tiles[i].widget_state);
        }
        if (tiles[i].widget_type && tiles[i].widget_type->releaseConfig)
            tiles[i].widget_type->releaseConfig(tiles[i].widget_cfg.data);
        tiles[i].widget_type = nullptr;
#if HAS_IMAGE_FETCH || HAS_IMAGE_LIBRARY
        // Cancel fetch slot first (stops background task from touching buffers)
#if HAS_IMAGE_FETCH
    if (tiles[i].bg_image) lv_image_set_src(tiles[i].bg_image, nullptr);
        if (tiles[i].image_slot != IMAGE_SLOT_INVALID) {
            image_fetch_cancel(tiles[i].image_slot);
            tiles[i].image_slot = IMAGE_SLOT_INVALID;
        }
        free(tiles[i].image_url_template);
        tiles[i].image_url_template = nullptr;
#endif
#if HAS_IMAGE_LIBRARY
#endif
#endif
        // Delete LVGL objects before freeing pixel data they reference
        if (tiles[i].tap_flash_timer) {
            lv_timer_delete(tiles[i].tap_flash_timer);
            tiles[i].tap_flash_timer = nullptr;
        }
        if (tiles[i].shadow) {
            lv_obj_delete(tiles[i].shadow);
            tiles[i].shadow = nullptr;
        }
        if (tiles[i].obj) {
            lv_obj_delete(tiles[i].obj);
            tiles[i].obj = nullptr;
        }
#if HAS_IMAGE_FETCH
        tiles[i].bg_image = nullptr;
#endif
#if HAS_IMAGE_LIBRARY
    tiles[i].local_bg_image = nullptr;
    tiles[i].local_image_template[0] = '\0';
    tiles[i].local_image_path[0] = '\0';
    memset(&tiles[i].local_img_dsc, 0, sizeof(tiles[i].local_img_dsc));
        if (tiles[i].local_image_slot != LOCAL_IMAGE_SLOT_INVALID) {
            local_image_loader_cancel(tiles[i].local_image_slot);
            tiles[i].local_image_slot = LOCAL_IMAGE_SLOT_INVALID;
        }
        if (tiles[i].local_image_pending_slot != LOCAL_IMAGE_SLOT_INVALID) {
            local_image_loader_cancel(tiles[i].local_image_pending_slot);
            tiles[i].local_image_pending_slot = LOCAL_IMAGE_SLOT_INVALID;
        }
#endif
#if HAS_IMAGE_FETCH || HAS_IMAGE_LIBRARY
        // Pixel data is owned by the image loader's LVGL buffer.
        memset(&tiles[i].img_dsc, 0, sizeof(tiles[i].img_dsc));
#endif
    }
    tileCount = 0;
    mainButtonCount = 0;
    bindingCount = 0;
    colorBindingCount = 0;
    numberBindingCount = 0;
    btnStateBindingCount = 0;
    hasTimeBinding = false;
    tilesBuilt = false;
    fontCache.clear();
    icon_store_collect_retired(pageIndex);
}

void PadScreen::buildTiles() {
    clearTiles();
    swipe_actions_register(screen);

    if (!container) return;

    // Ensure lazy arrays are allocated before building
    if (!allocateArrays()) {
        LOGE(TAG, "Pad %u: OOM for binding arrays", pageIndex);
        tilesBuilt = true; // Mark built (empty) to avoid retrying every frame
        return;
    }

    const PadConfig* cfg = pad_config_acquire(pageIndex);
    if (!cfg) {
        LOGE(TAG, "Pad %u config unavailable", pageIndex);
        configError = lv_label_create(container);
        lv_label_set_text(configError, "Pad configuration unavailable");
        lv_obj_center(configError);
        tilesBuilt = true; // Mark built (empty) to avoid retrying every frame
        return;
    }
    mainButtonCount = cfg->main_button_count;

    const bool swipes_enabled = !pad_disables_swipes(*cfg);
    swipe_actions_register(screen, swipes_enabled);

    // Cache page-level settings
    strlcpy(wakeScreen, cfg->wake_screen, sizeof(wakeScreen));
    strlcpy(pageBgTemplate, cfg->bg_color, sizeof(pageBgTemplate));
    { uint32_t bg = widget_default_rgb(DEFAULT_PAD_BG_COLOR); parse_hex_color(cfg->bg_color, &bg); pageBgDefault = bg; }
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
        LOGW(TAG, "Pad %u: unsupported layout '%s', skipping", pageIndex, cfg->layout);
        pad_config_release(cfg);
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

    const PadGridLayoutSpace layout_space = pad_layout_space(cfg->layout_settings);

    pad_compute_grid(
        cfg->cols, cfg->rows,
        (uint16_t)disp_w, (uint16_t)disp_h,
        cols_arr, rows_arr, cs_arr, rs_arr,
        cfg->button_count, rects, &layout_space,
        cfg->column_widths_px, cfg->row_heights_px);

    const UIScaleInfo& scale = pad_get_scale_info();

    // Create button tiles
    for (uint8_t i = 0; i < cfg->button_count && i < MAX_PAD_BUTTONS; i++) {
        const ScreenButtonConfig& bcfg = cfg->buttons[i];
        const PadRect& r = rects[i];
        ButtonTile& tile = tiles[i];
        tile.content_layout_dirty = false;
        tile.content_layout = {};
        tile.touch_start_valid = false;
        tile.shadow = nullptr;
        tile.shadow_follows_background = cfg->shadow.color_mode == BUTTON_SHADOW_COLOR_DARKEN_BACKGROUND;
        tile.shadow_darken_pct = cfg->shadow.darken_pct;
        const int16_t ui_ofs_x = bcfg.ui_offset_x;
        const int16_t ui_ofs_y = bcfg.ui_offset_y;
        const bool has_shadow = button_shadow_enabled(*cfg, bcfg);
        const int16_t shadow_offset_x = cfg->shadow.offset_x_px;
        const int16_t shadow_offset_y = cfg->shadow.offset_y_px;
        const uint8_t drop_blur = cfg->shadow.drop_blur_px;

        uint32_t bg_def = widget_default_rgb(DEFAULT_BUTTON_BG_COLOR); parse_hex_color(bcfg.bg_color, &bg_def);
        uint32_t shadow_def = widget_default_rgb(DEFAULT_BUTTON_SHADOW_COLOR);
        if (tile.shadow_follows_background) shadow_def = button_shadow_darken_color(bg_def, tile.shadow_darken_pct);
        else parse_hex_color(cfg->shadow.color, &shadow_def);
        const lv_coord_t cr_def = (lv_coord_t)strtol(bcfg.corner_radius, nullptr, 10);
        if (has_shadow && cfg->shadow.type == BUTTON_SHADOW_TYPE_OPAQUE) {
            tile.shadow = lv_obj_create(container);
            lv_obj_set_pos(tile.shadow, r.x + shadow_offset_x,
                           r.y + shadow_offset_y);
            lv_obj_set_size(tile.shadow, r.w, r.h);
            lv_obj_clear_flag(tile.shadow, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(tile.shadow, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(tile.shadow, rgb_to_lv(shadow_def), 0);
            lv_obj_set_style_bg_opa(tile.shadow, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(tile.shadow, 0, 0);
            lv_obj_set_style_radius(tile.shadow, cr_def, 0);
            lv_obj_set_style_clip_corner(tile.shadow, true, 0);
        }

        // Create tile container
        bool has_actions = false;
        for (uint8_t action_index = 0; action_index < bcfg.action_count; ++action_index)
            has_actions |= bcfg.actions[action_index].type[0] != '\0';
        for (uint8_t action_index = 0; action_index < bcfg.lp_action_count; ++action_index)
            has_actions |= bcfg.lp_actions[action_index].type[0] != '\0';
        const bool native_button_content = !bcfg.widget.type[0] ||
            strcmp(bcfg.widget.type, "gauge") == 0 ||
            strcmp(bcfg.widget.type, "sparkline") == 0 ||
            strcmp(bcfg.widget.type, "bar_chart") == 0 ||
            strcmp(bcfg.widget.type, "camera_preview") == 0;
        const bool native_button = !DISPLAY_DISABLE_ANIMATIONS &&
            native_button_content && has_actions;
        lv_obj_t* obj = native_button ? lv_button_create(container) : lv_obj_create(container);
        lv_obj_set_user_data(obj, this);
        tile.obj = obj;
        tileCount++;
        lv_obj_add_event_cb(obj, onPress, LV_EVENT_ALL, &tiles[i]);
        lv_obj_set_pos(obj, r.x, r.y);
        lv_obj_set_size(obj, r.w, r.h);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

        // Styling — compute initial colors from config strings
        uint32_t fg_def = widget_default_rgb(DEFAULT_BUTTON_FG_COLOR); parse_hex_color(bcfg.fg_color, &fg_def);
        uint32_t border_def = widget_default_rgb(DEFAULT_BUTTON_BORDER_COLOR); parse_hex_color(bcfg.border_color, &border_def);
        lv_obj_set_style_bg_color(obj, rgb_to_lv(bg_def), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(obj, rgb_to_lv(border_def), 0);
        lv_coord_t bw_def = (lv_coord_t)strtol(bcfg.border_width, nullptr, 10);
        lv_obj_set_style_border_width(obj, bw_def, 0);
        lv_obj_set_style_radius(obj, cr_def, 0);
        lv_obj_set_style_clip_corner(obj, true, 0);
        lv_obj_set_style_shadow_width(obj, 0, 0);
        if (native_button) {
            static const lv_style_prop_t press_properties[] = {
                LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT,
                LV_STYLE_RECOLOR, LV_STYLE_RECOLOR_OPA, 0
            };
            static lv_style_transition_dsc_t press_transition;
            lv_style_transition_dsc_init(&press_transition, press_properties,
                                         lv_anim_path_linear, 30, 0, nullptr);
            const bool is_light = perceived_luminance(bg_def) > TAP_LUMINANCE_THRESH;
            lv_obj_set_style_recolor(obj, is_light ? lv_color_black() : lv_color_white(), LV_STATE_PRESSED);
            lv_obj_set_style_recolor_opa(obj, is_light ? TAP_OVERLAY_DARK_OPA : TAP_OVERLAY_LIGHT_OPA,
                                         LV_STATE_PRESSED);
            lv_obj_set_style_transition(obj, &press_transition, LV_STATE_PRESSED);
        }
        tile.fill_style = fill_style_create(obj, bcfg.fill_style);
        if (bcfg.fill_style && !tile.fill_style) LOGW(TAG, "Button styling allocation failed");
        if (has_shadow && cfg->shadow.type == BUTTON_SHADOW_TYPE_DROP) {
            lv_obj_set_style_shadow_color(obj, rgb_to_lv(shadow_def), 0);
            lv_obj_set_style_shadow_opa(obj, LV_OPA_70, 0);
            lv_obj_set_style_shadow_width(obj, drop_blur, 0);
            lv_obj_set_style_shadow_spread(obj, 0, 0);
            lv_obj_set_style_shadow_offset_x(obj, shadow_offset_x, 0);
            lv_obj_set_style_shadow_offset_y(obj, shadow_offset_y, 0);
        }

        // Content padding — plain px inset for labels/icon/widget. Cascades
        // button → device defaults → firmware default "4" (the legacy fixed inset).
        long cp_raw = strtol(bcfg.content_pad, nullptr, 10);
        if (cp_raw < 0) cp_raw = 0; else if (cp_raw > 50) cp_raw = 50;
        const int16_t pad = (int16_t)cp_raw;
        lv_obj_set_style_pad_all(obj, pad, 0);

        lv_color_t fg = rgb_to_lv(fg_def);
        lv_obj_set_style_text_color(obj, fg, 0);

        lv_obj_t* lbl_top = create_content_label(obj, bcfg.label_top, bcfg.style_top,
            scale.font_small, fg, fontCache, PAD_LABEL_ANCHOR_TOP);
        lv_obj_t* lbl_center = create_content_label(obj, bcfg.label_center, bcfg.style_center,
            scale.font_large, fg, fontCache, PAD_LABEL_ANCHOR_CENTER);

        // Icon image (shown when a source is set and its generated PNG is cached)
        lv_obj_t* icon_img = nullptr;
        uint32_t icon_def = fg_def;
        const bool icon_follows_text = !bcfg.icon_color[0] || strcmp(bcfg.icon_color, "auto") == 0;
        if (!icon_follows_text) parse_hex_color(bcfg.icon_color, &icon_def);
        uint16_t icon_scale = LV_SCALE_NONE;
        if (bcfg.icon_source_id[0]) {
            char icon_key[CONFIG_ICON_KEY_MAX_LEN];
            icon_store_build_key(pageIndex, i >= cfg->main_button_count ? "overlay" : "main",
                                 bcfg.col, bcfg.row,
                                 icon_key, sizeof(icon_key));
            IconRef ref;
            if (icon_store_lookup(icon_key, &ref)) {
                icon_img = lv_image_create(obj);
                lv_image_set_src(icon_img, ref.dsc);
                icon_scale = capped_icon_scale(bcfg, r, pad, ref);
                lv_image_set_scale(icon_img, icon_scale);
                lv_obj_clear_flag(icon_img, LV_OBJ_FLAG_CLICKABLE);
                if (ref.kind == ICON_KIND_MONO) {
                    lv_obj_set_style_image_recolor(icon_img, rgb_to_lv(icon_def), 0);
                    lv_obj_set_style_image_recolor_opa(icon_img, LV_OPA_COVER, 0);
                    tile.icon_is_mono = true;
                }
            }
        }

        const WidgetType* selected_widget = bcfg.widget.type[0] ? widget_find(bcfg.widget.type) : nullptr;
        lv_obj_t* lbl_bottom = create_content_label(obj, bcfg.label_bottom, bcfg.style_bottom,
            scale.font_small, fg, fontCache, PAD_LABEL_ANCHOR_BOTTOM);

        // Store runtime data
        tile.obj = obj;
        tile.label_top = lbl_top;
        tile.label_center = lbl_center;
        tile.label_bottom = lbl_bottom;
        tile.icon_img = icon_img;
        tile.icon_follows_text = icon_follows_text;
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

        ButtonContentLayout content_layout{};
        lv_obj_t* labels[] = {tile.label_top, tile.label_center, tile.label_bottom};
        content_layout = pad_apply_content_layout(obj, bcfg, icon_img, labels, scale, &fontCache,
                                                  &tile.content_layout, tile.icon_is_mono);

        // Widget initialization
        tile.widget_type = nullptr;
        memset(&tile.widget_state, 0, sizeof(WidgetState));
        for (int wb = 0; wb < MAX_WIDGET_BINDINGS; wb++) tile.widget_binding[wb][0] = '\0';
        tile.widget_last[0] = '\0';
        if (bcfg.widget.type[0]) {
            const WidgetType* wt = selected_widget;
            if (wt) {
                tile.widget_type = wt;
                memcpy(&tile.widget_cfg, &bcfg.widget, sizeof(WidgetConfig));
                if (wt->retainConfig) wt->retainConfig(tile.widget_cfg.data);
#if HAS_DISPLAY
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
                    tile.widget_state.font_owner = &fontCache;
                    lv_obj_t* widget_parent = button_content_region(obj, content_layout.widget, ui_ofs_x, ui_ofs_y);
                    lv_obj_set_style_bg_color(widget_parent, lv_obj_get_style_bg_color(obj, LV_PART_MAIN), 0);
                    lv_obj_set_style_text_color(widget_parent, lv_obj_get_style_text_color(obj, LV_PART_MAIN), 0);
                    lv_obj_set_user_data(widget_parent, this);
                    tile.content_layout.widget_region = widget_parent;
                    if (widget_owns_input(wt)) {
                        lv_obj_add_flag(widget_parent, LV_OBJ_FLAG_CLICKABLE);
                        lv_obj_add_event_cb(widget_parent, onPress,
                            static_cast<lv_event_code_t>(LV_EVENT_ALL | LV_EVENT_PREPROCESS), &tile);
                    }
                    wt->createUI(widget_parent, &tile.widget_cfg, &bcfg, &r, &scale, &tile.widget_state);
                    if (strcmp(wt->name, "option_selector") == 0)
                        option_selector_attach(&tile.widget_state, tile.actions, tile.action_count,
                                               pageIndex, i >= cfg->main_button_count, bcfg.col, bcfg.row,
                                               PadScreen::onPress, &tile, pageBindings, pageBindingCount);
#if HAS_TOUCH
                    if (strcmp(wt->name, "slider") == 0)
                        slider_attach(&tile.widget_state, tile.actions, tile.action_count,
                                      PadScreen::onPress, &tile);
#endif
                }
            }
        }

#if HAS_DISPLAY
        // Register template-based label bindings for labels containing [scheme:...]
        auto addTemplateBinding = [this, i](lv_obj_t* lbl, const char* label_text,
                                         uint8_t labelIndex) {
            if (!lbl || !label_text || !label_text[0]) return;
            if (!binding_template_has_bindings(label_text)) return;
            if (bindingCount >= MAX_BINDINGS) return;
            RuntimeLabelBinding& rb = bindings[bindingCount];
            strlcpy(rb.templ, label_text, sizeof(rb.templ));
            strlcpy(rb.last, "---", sizeof(rb.last));
            rb.tileIndex = i;
            rb.labelIndex = labelIndex;
            bindingCount++;
            // Show placeholder until first resolve
            pad_set_content_label_text(tiles[i].content_layout, labelIndex, "---");
            tiles[i].content_layout_dirty = true;
        };
        addTemplateBinding(tile.label_top, bcfg.label_top, 0);
        addTemplateBinding(tile.label_center, bcfg.label_center, 1);
        addTemplateBinding(tile.label_bottom, bcfg.label_bottom, 2);

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
        if (tile.icon_is_mono && !tile.icon_follows_text &&
            binding_template_has_bindings(bcfg.icon_color))
            addColorBinding(i, bcfg.icon_color, icon_def, 7);
        if (bcfg.icon_badge_padding && tile.content_layout.icon_region &&
            !tile.content_layout.icon_badge_auto_color) {
            uint32_t badge_color = lv_color_to_u32(pad_auto_icon_badge_color(tile.obj, tile.content_layout));
            parse_hex_color(bcfg.icon_badge_color, &badge_color);
            addColorBinding(i, bcfg.icon_badge_color, badge_color, 6);
        }

        // Per-label text colors. Only for normal button tiles (widget tiles manage
        // their own labels, which may be null). A per-label color — static or
        // binding — sets an override bit so the fg (target=1) poll does not clobber
        // the label's text color. Binding-valued colors are additionally registered
        // as runtime color bindings (targets 3/4/5) resolved live each poll cycle.
        {
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

#if HAS_IMAGE_FETCH || HAS_IMAGE_LIBRARY
        // Request background image fetch if URL is configured
#if HAS_IMAGE_FETCH
        tile.bg_image = nullptr;
        tile.image_slot = IMAGE_SLOT_INVALID;
        tile.image_url_template = nullptr;
#endif
#if HAS_IMAGE_LIBRARY
        tile.local_bg_image = nullptr;
        tile.local_image_slot = LOCAL_IMAGE_SLOT_INVALID;
        tile.local_image_pending_slot = LOCAL_IMAGE_SLOT_INVALID;
        strlcpy(tile.local_image_template, bcfg.bg_image_path, sizeof(tile.local_image_template));
        tile.local_image_path[0] = '\0';
        tile.local_image_scale_mode = bcfg.bg_image_letterbox ? IMAGE_SCALE_LETTERBOX : IMAGE_SCALE_COVER;
        tile.local_image_letterbox_color = lv_color_to_u16(rgb_to_lv(bg_def));
        memset(&tile.local_img_dsc, 0, sizeof(tile.local_img_dsc));
#endif
        memset(&tile.img_dsc, 0, sizeof(tile.img_dsc));

#if HAS_IMAGE_LIBRARY
        if (bcfg.bg_image_path[0] && !binding_template_has_bindings(bcfg.bg_image_path) &&
            ImageLibraryCatalog::is_image_path(bcfg.bg_image_path)) {
            tile.local_image_slot = local_image_loader_request(
                bcfg.bg_image_path, r.w, r.h, tile.local_image_scale_mode,
                tile.local_image_letterbox_color);
            if (tile.local_image_slot != LOCAL_IMAGE_SLOT_INVALID) {
                tile.local_bg_image = lv_image_create(obj);
                lv_obj_set_size(tile.local_bg_image, r.w, r.h);
                lv_obj_set_align(tile.local_bg_image, LV_ALIGN_CENTER);
                lv_obj_clear_flag(tile.local_bg_image, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_move_to_index(tile.local_bg_image, 0);
                strlcpy(tile.local_image_path, bcfg.bg_image_path, sizeof(tile.local_image_path));
                LOGT(TAG, "Button %u: local image slot %d for %.40s", i, tile.local_image_slot, bcfg.bg_image_path);
            }
        }
#endif

#if HAS_IMAGE_FETCH
        if (bcfg.bg_image_url[0]) {
            const bool dynamic = binding_template_has_bindings(bcfg.bg_image_url);
            if (dynamic && !bcfg.bg_image_user[0] && !bcfg.bg_image_password[0]) {
                const size_t size = strlen(bcfg.bg_image_url) + 1;
                tile.image_url_template = (char*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (tile.image_url_template) memcpy(tile.image_url_template, bcfg.bg_image_url, size);
            }
            ImageScaleMode sm = bcfg.bg_image_letterbox ? IMAGE_SCALE_LETTERBOX : IMAGE_SCALE_COVER;
            if (!dynamic || tile.image_url_template) {
                tile.image_slot = image_fetch_request(
                    dynamic ? "" : bcfg.bg_image_url,
                    dynamic ? "" : bcfg.bg_image_user, dynamic ? "" : bcfg.bg_image_password,
                    r.w, r.h, bcfg.bg_image_interval_ms, sm, lv_color_to_u16(rgb_to_lv(bg_def)));
            }

            if (tile.image_slot != IMAGE_SLOT_INVALID) {
                // Create LVGL image widget as background (behind labels)
                tile.bg_image = lv_image_create(obj);
                lv_obj_add_flag(tile.bg_image, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_size(tile.bg_image, r.w, r.h);
                lv_obj_set_align(tile.bg_image, LV_ALIGN_CENTER);
                lv_obj_clear_flag(tile.bg_image, LV_OBJ_FLAG_CLICKABLE);
                // Move to back so labels render on top
                lv_obj_move_to_index(tile.bg_image,
#if HAS_IMAGE_LIBRARY
                                     tile.local_bg_image ? 1 : 0
#else
                                     0
#endif
                );

                LOGT(TAG, "Button %u: image slot %d", i, tile.image_slot);
            } else {
                LOGW(TAG, "Button %u: image fetch slot unavailable", i);
            }
        }
#endif
#endif

        // Tap overlay — semi-transparent sheet shown briefly on press.
        // Created last so it renders on top of all children (image bg, widgets, labels).
        // Color adapts to background luminance: dark overlay on light bg, light on dark.
        if (!native_button) {
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
            if (button_overlay_uses_fade(tile.widget_cfg.type)) {
                static const lv_style_prop_t fade_properties[] = {LV_STYLE_OPA, 0};
                static lv_style_transition_dsc_t fade_in;
                static lv_style_transition_dsc_t fade_out;
                lv_style_transition_dsc_init(&fade_in, fade_properties, lv_anim_path_linear, 30, 0, nullptr);
                lv_style_transition_dsc_init(&fade_out, fade_properties, lv_anim_path_linear, 80, 0, nullptr);
                lv_obj_set_style_opa(ov, LV_OPA_TRANSP, 0);
                lv_obj_set_style_opa(ov, LV_OPA_COVER, LV_STATE_USER_1);
                lv_obj_set_style_transition(ov, &fade_out, 0);
                lv_obj_set_style_transition(ov, &fade_in, LV_STATE_USER_1);
            }
            lv_obj_clear_flag(ov, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
            lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
            tile.tap_overlay = ov;
            tile.tap_flash_x = -inset;
            tile.tap_flash_y = -inset;
            tile.tap_flash_w = r.w;
            tile.tap_flash_h = r.h;
            if (strcmp(tile.widget_cfg.type, "gamepad_button") == 0) {
                lv_obj_add_event_cb(obj, onWidgetPressFeedback, LV_EVENT_VALUE_CHANGED, &tiles[i]);
            }
        }
        if (strcmp(tile.widget_cfg.type, "slider") == 0 ||
            strcmp(tile.widget_cfg.type, "option_selector") == 0) {
            lv_obj_add_event_cb(obj, onWidgetPressFeedback, LV_EVENT_VALUE_CHANGED, &tiles[i]);
        }

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
        lv_obj_add_event_cb(padActionOverlay, onSwipeGuard, LV_EVENT_GESTURE, this);
        swipe_actions_register(padActionOverlay, swipes_enabled);
        lv_obj_move_to_index(padActionOverlay, 0);
    }

    createOverlayInputShield();

#if HAS_DISPLAY
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

    pad_config_release(cfg);

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
            LOGT(TAG, "Pad %u: device-class '%s' consumer detected — will hold engine while visible",
                 pageIndex, dc->name);
        }
    }

    hasTimeBinding = containsTimeBinding();

    tilesBuilt = true;

    LOGI(TAG, "Pad %u: built %u buttons (%dx%d display)", pageIndex, tileCount, disp_w, disp_h);
}
