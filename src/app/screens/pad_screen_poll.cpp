#include "pad_screen.h"
#if HAS_IMAGE_FETCH
#include "../image_fetch.h"
#include <esp_heap_caps.h>
#include <string.h>
#endif

// TAG, perceived_luminance(), rgb_to_lv(), and tap-overlay constants are
// defined in pad_screen.cpp which is #included before this file in screens.cpp.

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
    // Multi-slot widgets use binding_2+ for additional slots.
    // Preserve empty intermediate slots so widgets can distinguish
    // "ring 3 only" from promoted ring 2 behavior.
    char widget_resolved[BINDING_TEMPLATE_MAX_LEN];
    char widget_combined[BINDING_TEMPLATE_MAX_LEN * MAX_WIDGET_BINDINGS + MAX_WIDGET_BINDINGS + 1];

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
