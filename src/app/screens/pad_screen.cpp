#include "pad_screen.h"
#include "../display_manager.h"
#include "../icon_store.h"
#include "../log_manager.h"
#include "../pad_config.h"
#include "../pad_layout.h"
#if HAS_MQTT
#include "../mqtt_manager.h"
#include "../mqtt_sub_store.h"
#include <ArduinoJson.h>
#endif
#if HAS_IMAGE_FETCH
#include "../image_fetch.h"
#endif

#include <esp_heap_caps.h>
#include <string.h>

#define TAG "PadScr"

// Tap flash: brighten 30% for 100ms
#define TAP_FLASH_BRIGHTEN_PCT 30
#define TAP_FLASH_DURATION_MS  100

// ============================================================================
// Helpers
// ============================================================================

// Brighten a color by a percentage (clamp to 255)
static lv_color_t brighten_color(uint32_t rgb, uint8_t pct) {
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    r = (uint8_t)(r + (uint16_t)(255 - r) * pct / 100);
    g = (uint8_t)(g + (uint16_t)(255 - g) * pct / 100);
    b = (uint8_t)(b + (uint16_t)(255 - b) * pct / 100);
    return lv_color_make(r, g, b);
}

static lv_color_t rgb_to_lv(uint32_t rgb) {
    return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// ============================================================================
// PadScreen
// ============================================================================

PadScreen::PadScreen(uint8_t page, DisplayManager* manager)
    : pageIndex(page), displayMgr(manager), screen(nullptr), container(nullptr),
      tileCount(0), bindingCount(0), stateBindingCount(0), cachedGeneration(UINT32_MAX), tilesBuilt(false) {
    memset(tiles, 0, sizeof(tiles));
    memset(bindings, 0, sizeof(bindings));
    memset(stateBindings, 0, sizeof(stateBindings));
}

PadScreen::~PadScreen() {
    destroy();
}

void PadScreen::create() {
    if (screen) return;

    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
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
        // Config unchanged — just poll MQTT bindings and toggle state
        pollMqttBindings();
        pollToggleState();
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
    stateBindingCount = 0;
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
        free(cfg);
        tilesBuilt = true; // Mark built (empty) to avoid retrying every frame
        return;
    }

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

        // Styling
        lv_obj_set_style_bg_color(obj, rgb_to_lv(bcfg.bg_color_rgb), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(obj, rgb_to_lv(bcfg.border_color_rgb), 0);
        lv_obj_set_style_border_width(obj, bcfg.border_width_px, 0);
        lv_obj_set_style_radius(obj, bcfg.corner_radius_px, 0);
        lv_obj_set_style_pad_all(obj, 4, 0);

        lv_color_t fg = rgb_to_lv(bcfg.fg_color_rgb);

        // Top label
        lv_obj_t* lbl_top = nullptr;
        if (bcfg.label_top[0]) {
            lbl_top = lv_label_create(obj);
            lv_label_set_text(lbl_top, bcfg.label_top);
            lv_obj_set_style_text_color(lbl_top, fg, 0);
            lv_obj_set_style_text_font(lbl_top, scale.font_small, 0);
            lv_obj_align(lbl_top, LV_ALIGN_TOP_MID, 0, 0);
            lv_label_set_long_mode(lbl_top, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(lbl_top, r.w - 8);
            lv_obj_set_style_text_align(lbl_top, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_clear_flag(lbl_top, LV_OBJ_FLAG_CLICKABLE);
        }

        // Center label (shown when no icon)
        lv_obj_t* lbl_center = nullptr;
        if (bcfg.label_center[0] && !bcfg.icon_id[0]) {
            lbl_center = lv_label_create(obj);
            lv_label_set_text(lbl_center, bcfg.label_center);
            lv_obj_set_style_text_color(lbl_center, fg, 0);
            lv_obj_set_style_text_font(lbl_center, scale.font_large, 0);
            lv_obj_align(lbl_center, LV_ALIGN_CENTER, 0, 0);
            lv_label_set_long_mode(lbl_center, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(lbl_center, r.w - 8);
            lv_obj_set_style_text_align(lbl_center, LV_TEXT_ALIGN_CENTER, 0);
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
                const int16_t top_h = bcfg.label_top[0] ?
                    lv_font_get_line_height(scale.font_small) : 0;
                const int16_t bot_h = bcfg.label_bottom[0] ?
                    lv_font_get_line_height(scale.font_small) : 0;
                const int16_t y_ofs = (top_h - bot_h) / 2;
                lv_obj_align(icon_img, LV_ALIGN_CENTER, 0, y_ofs);

                lv_obj_clear_flag(icon_img, LV_OBJ_FLAG_CLICKABLE);
                if (ref.kind == ICON_KIND_MONO) {
                    lv_obj_set_style_image_recolor(icon_img, fg, 0);
                    lv_obj_set_style_image_recolor_opa(icon_img, LV_OPA_COVER, 0);
                }
            }
        }

        // Bottom label
        lv_obj_t* lbl_bottom = nullptr;
        if (bcfg.label_bottom[0]) {
            lbl_bottom = lv_label_create(obj);
            lv_label_set_text(lbl_bottom, bcfg.label_bottom);
            lv_obj_set_style_text_color(lbl_bottom, fg, 0);
            lv_obj_set_style_text_font(lbl_bottom, scale.font_small, 0);
            lv_obj_align(lbl_bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_label_set_long_mode(lbl_bottom, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(lbl_bottom, r.w - 8);
            lv_obj_set_style_text_align(lbl_bottom, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_clear_flag(lbl_bottom, LV_OBJ_FLAG_CLICKABLE);
        }

        // Store runtime data
        tile.obj = obj;
        tile.label_top = lbl_top;
        tile.label_center = lbl_center;
        tile.label_bottom = lbl_bottom;
        tile.icon_img = icon_img;
        tile.bg_color_rgb = bcfg.bg_color_rgb;
        tile.page = pageIndex;
        tile.col = bcfg.col;
        tile.row = bcfg.row;
        memcpy(&tile.action, &bcfg.action, sizeof(ButtonAction));
        memcpy(&tile.lp_action, &bcfg.lp_action, sizeof(ButtonAction));

#if HAS_MQTT
        // Create labels for MQTT bindings where no static label text was set
        if (bcfg.label_top_bind.mqtt_topic[0] && !tile.label_top) {
            tile.label_top = lv_label_create(obj);
            lv_label_set_text(tile.label_top, "");
            lv_obj_set_style_text_color(tile.label_top, fg, 0);
            lv_obj_set_style_text_font(tile.label_top, scale.font_small, 0);
            lv_obj_align(tile.label_top, LV_ALIGN_TOP_MID, 0, 0);
            lv_label_set_long_mode(tile.label_top, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(tile.label_top, r.w - 8);
            lv_obj_set_style_text_align(tile.label_top, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_clear_flag(tile.label_top, LV_OBJ_FLAG_CLICKABLE);
        }
        if (bcfg.label_center_bind.mqtt_topic[0] && !tile.label_center && !bcfg.icon_id[0]) {
            tile.label_center = lv_label_create(obj);
            lv_label_set_text(tile.label_center, "");
            lv_obj_set_style_text_color(tile.label_center, fg, 0);
            lv_obj_set_style_text_font(tile.label_center, scale.font_large, 0);
            lv_obj_align(tile.label_center, LV_ALIGN_CENTER, 0, 0);
            lv_label_set_long_mode(tile.label_center, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(tile.label_center, r.w - 8);
            lv_obj_set_style_text_align(tile.label_center, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_clear_flag(tile.label_center, LV_OBJ_FLAG_CLICKABLE);
        }
        if (bcfg.label_bottom_bind.mqtt_topic[0] && !tile.label_bottom) {
            tile.label_bottom = lv_label_create(obj);
            lv_label_set_text(tile.label_bottom, "");
            lv_obj_set_style_text_color(tile.label_bottom, fg, 0);
            lv_obj_set_style_text_font(tile.label_bottom, scale.font_small, 0);
            lv_obj_align(tile.label_bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_label_set_long_mode(tile.label_bottom, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(tile.label_bottom, r.w - 8);
            lv_obj_set_style_text_align(tile.label_bottom, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_clear_flag(tile.label_bottom, LV_OBJ_FLAG_CLICKABLE);
        }

        // Register MQTT label bindings
        auto addBinding = [this](lv_obj_t* lbl, const LabelBinding& bind) {
            if (!lbl || !bind.mqtt_topic[0]) return;
            if (bindingCount >= MAX_BINDINGS) return;
            RuntimeLabelBinding& rb = bindings[bindingCount];
            rb.label = lbl;
            strlcpy(rb.mqtt_topic, bind.mqtt_topic, sizeof(rb.mqtt_topic));
            strlcpy(rb.json_path, bind.json_path, sizeof(rb.json_path));
            strlcpy(rb.format, bind.format, sizeof(rb.format));
            rb.active = true;
            bindingCount++;
        };
        addBinding(tile.label_top, bcfg.label_top_bind);
        addBinding(tile.label_center, bcfg.label_center_bind);
        addBinding(tile.label_bottom, bcfg.label_bottom_bind);

        // Register toggle state binding
        if (bcfg.state_bind.mqtt_topic[0] && stateBindingCount < MAX_PAD_BUTTONS) {
            RuntimeStateBinding& sb = stateBindings[stateBindingCount];
            sb.tileIndex = i;
            strlcpy(sb.mqtt_topic, bcfg.state_bind.mqtt_topic, sizeof(sb.mqtt_topic));
            strlcpy(sb.json_path, bcfg.state_bind.json_path, sizeof(sb.json_path));
            strlcpy(sb.on_value, bcfg.state_bind.on_value, sizeof(sb.on_value));
            sb.fg_color_rgb = bcfg.fg_color_rgb;
            sb.disabled_fg_color_rgb = bcfg.disabled_fg_color_rgb;
            sb.currentlyOn = true;  // assume ON until first message
            sb.initialized = false;
            sb.active = true;
            stateBindingCount++;
        }
#endif

        // Event handlers — store tile index in user_data
        // Pack page index and tile index together for the callback
        uintptr_t user = ((uintptr_t)this);
        lv_obj_set_user_data(obj, (void*)user);

        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(obj, onTap, LV_EVENT_SHORT_CLICKED, &tiles[i]);
        lv_obj_add_event_cb(obj, onLongPress, LV_EVENT_LONG_PRESSED, &tiles[i]);

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

        tileCount++;
    }

    free(cfg);
    tilesBuilt = true;

    LOGI(TAG, "Page %u: built %u tiles (%dx%d display)", pageIndex, tileCount, disp_w, disp_h);
}

// ============================================================================
// Event Handlers
// ============================================================================

// Tap flash: temporarily brighten, then restore after timeout
struct TapFlashCtx {
    lv_obj_t* obj;
    uint32_t original_rgb;
};

void PadScreen::tapFlashTimerCb(lv_timer_t* timer) {
    TapFlashCtx* ctx = (TapFlashCtx*)lv_timer_get_user_data(timer);
    if (ctx && ctx->obj) {
        lv_obj_set_style_bg_color(ctx->obj, rgb_to_lv(ctx->original_rgb), 0);
    }
    free(ctx);
    lv_timer_delete(timer);
}

static void do_tap_flash(lv_obj_t* obj, uint32_t bg_rgb) {
    lv_obj_set_style_bg_color(obj, brighten_color(bg_rgb, TAP_FLASH_BRIGHTEN_PCT), 0);

    TapFlashCtx* ctx = (TapFlashCtx*)malloc(sizeof(TapFlashCtx));
    if (ctx) {
        ctx->obj = obj;
        ctx->original_rgb = bg_rgb;
        lv_timer_create(PadScreen::tapFlashTimerCb, TAP_FLASH_DURATION_MS, ctx);
    }
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
    do_tap_flash(tile->obj, tile->bg_color_rgb);

    execute_action(tile->action, "Tap");

#if HAS_MQTT
    publish_button_event(tile, "press");
#endif
}

void PadScreen::onLongPress(lv_event_t* e) {
    ButtonTile* tile = (ButtonTile*)lv_event_get_user_data(e);
    if (!tile || !tile->obj) return;

    // Tap flash
    do_tap_flash(tile->obj, tile->bg_color_rgb);

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
    if (bindingCount == 0) return;

    char payload[MQTT_SUB_STORE_MAX_VALUE_LEN];
    char extracted[128];
    char formatted[128];

    for (uint16_t i = 0; i < bindingCount; i++) {
        RuntimeLabelBinding& rb = bindings[i];
        if (!rb.active || !rb.label) continue;

        bool changed = false;
        if (!mqtt_sub_store_get(rb.mqtt_topic, payload, sizeof(payload), &changed)) continue;
        if (!changed) continue;

        // Extract value from JSON payload (or raw if path is ".")
        const char* path = rb.json_path[0] ? rb.json_path : ".";
        if (!mqtt_sub_store_extract_json(payload, path, extracted, sizeof(extracted))) {
            // Extraction failed — use raw payload
            strlcpy(extracted, payload, sizeof(extracted));
        }

        // Apply format string (best-effort)
        if (rb.format[0]) {
            mqtt_sub_store_format_value(extracted, rb.format, formatted, sizeof(formatted));
        } else {
            strlcpy(formatted, extracted, sizeof(formatted));
        }

        // Update LVGL label (we're already inside the LVGL task / mutex)
        lv_label_set_text(rb.label, formatted);
    }
#endif
}

// ============================================================================
// Toggle State Polling
// ============================================================================

void PadScreen::pollToggleState() {
#if HAS_MQTT
    if (stateBindingCount == 0) return;

    char payload[MQTT_SUB_STORE_MAX_VALUE_LEN];
    char extracted[128];

    for (uint16_t i = 0; i < stateBindingCount; i++) {
        RuntimeStateBinding& sb = stateBindings[i];
        if (!sb.active) continue;

        bool changed = false;
        if (!mqtt_sub_store_get(sb.mqtt_topic, payload, sizeof(payload), &changed)) continue;
        if (!changed && sb.initialized) continue;

        // Extract value via JSON path
        const char* path = sb.json_path[0] ? sb.json_path : ".";
        if (!mqtt_sub_store_extract_json(payload, path, extracted, sizeof(extracted))) {
            strlcpy(extracted, payload, sizeof(extracted));
        }

        // Compare to on_value (case-sensitive)
        bool isOn = (strcmp(extracted, sb.on_value) == 0);

        // Only update LVGL if state actually changed (or first time)
        if (isOn == sb.currentlyOn && sb.initialized) continue;
        sb.currentlyOn = isOn;
        sb.initialized = true;

        // Apply fg color to all labels on this tile
        uint8_t ti = sb.tileIndex;
        if (ti >= tileCount) continue;
        ButtonTile& tile = tiles[ti];
        lv_color_t fg = rgb_to_lv(isOn ? sb.fg_color_rgb : sb.disabled_fg_color_rgb);

        if (tile.label_top) lv_obj_set_style_text_color(tile.label_top, fg, 0);
        if (tile.label_center) lv_obj_set_style_text_color(tile.label_center, fg, 0);
        if (tile.label_bottom) lv_obj_set_style_text_color(tile.label_bottom, fg, 0);
    }
#endif
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
