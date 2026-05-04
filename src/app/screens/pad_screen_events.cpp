#include "pad_screen.h"
#include "../action_dispatch.h"
#include "../display_manager.h"
#include "../log_manager.h"
#include "../swipe_actions.h"
#if HAS_AUDIO
#include "../audio.h"
#include "../config_manager.h"
extern DeviceConfig device_config;
#endif
#if HAS_MQTT
#include "../mqtt_manager.h"
#include <ArduinoJson.h>
#endif

// Forward declaration — defined in numericrocker_widget.cpp
void numericrocker_substitute_step(ButtonAction* act, float step);

#include "../widgets/numericrocker_zones.h"

// TAG and TAP_FLASH_DURATION_MS are defined in pad_screen.cpp which is
// #included before this file in screens.cpp.

// ============================================================================
// Action helpers
// ============================================================================

// Returns true if at least one action in the list has a non-empty type.
static bool has_any_action(const ButtonAction* acts, uint8_t count) {
    for (uint8_t i = 0; i < count; i++)
        if (acts[i].type[0]) return true;
    return false;
}

// Returns true if any action in the list produces its own audio (beep or sound).
static bool has_audio_action(const ButtonAction* acts, uint8_t count) {
    for (uint8_t i = 0; i < count; i++)
        if (strcmp(acts[i].type, ACTION_TYPE_BEEP) == 0 ||
            strcmp(acts[i].type, ACTION_TYPE_SOUND) == 0) return true;
    return false;
}

// ============================================================================
// Event Handlers
// ============================================================================

// Tap flash: show overlay briefly, then hide after timeout.
// For rocker widgets, the overlay is resized to cover only the tapped zone;
// on timeout we restore full size so the next flash starts clean.
struct TapFlashCtx {
    lv_obj_t* overlay;
    lv_coord_t orig_x;
    lv_coord_t orig_y;
    lv_coord_t orig_w;
    lv_coord_t orig_h;
};

void PadScreen::tapFlashTimerCb(lv_timer_t* timer) {
    auto* ctx = (TapFlashCtx*)lv_timer_get_user_data(timer);
    if (ctx && ctx->overlay) {
        lv_obj_add_flag(ctx->overlay, LV_OBJ_FLAG_HIDDEN);
        // Restore original size/position
        lv_obj_set_pos(ctx->overlay, ctx->orig_x, ctx->orig_y);
        lv_obj_set_size(ctx->overlay, ctx->orig_w, ctx->orig_h);
    }
    delete ctx;
    lv_timer_delete(timer);
}

// zone: 0 = full button, 1 = zone A (top/left), 2 = zone B (bottom/right)
// horizontal: only meaningful when zone != 0
static void do_tap_flash(ButtonTile* tile, uint8_t zone = 0, bool horizontal = false) {
    if (!tile->tap_overlay) return;

    lv_obj_t* ov = tile->tap_overlay;

    // Save original geometry
    auto* ctx = new TapFlashCtx();
    ctx->overlay = ov;
    ctx->orig_x = lv_obj_get_x(ov);
    ctx->orig_y = lv_obj_get_y(ov);
    ctx->orig_w = lv_obj_get_width(ov);
    ctx->orig_h = lv_obj_get_height(ov);

    // Resize to cover only the tapped zone for rocker
    if (zone == 1) {
        // Zone A: top half (vertical) or left half (horizontal)
        if (horizontal) {
            lv_obj_set_size(ov, ctx->orig_w / 2, ctx->orig_h);
        } else {
            lv_obj_set_size(ov, ctx->orig_w, ctx->orig_h / 2);
        }
    } else if (zone == 2) {
        // Zone B: bottom half (vertical) or right half (horizontal)
        if (horizontal) {
            lv_obj_set_pos(ov, ctx->orig_x + ctx->orig_w / 2, ctx->orig_y);
            lv_obj_set_size(ov, ctx->orig_w - ctx->orig_w / 2, ctx->orig_h);
        } else {
            lv_obj_set_pos(ov, ctx->orig_x, ctx->orig_y + ctx->orig_h / 2);
            lv_obj_set_size(ov, ctx->orig_w, ctx->orig_h - ctx->orig_h / 2);
        }
    }

    lv_obj_remove_flag(ov, LV_OBJ_FLAG_HIDDEN);
    lv_timer_t* t = lv_timer_create(PadScreen::tapFlashTimerCb, TAP_FLASH_DURATION_MS, ctx);
    if (!t) {
        lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(ov, ctx->orig_x, ctx->orig_y);
        lv_obj_set_size(ov, ctx->orig_w, ctx->orig_h);
        delete ctx;
    }
}

// Pixel-based tap flash: resize overlay to a pixel band of the tile.
// px_start/px_end are pixel offsets along the given axis.
static void do_tap_flash_px(ButtonTile* tile, int px_start, int px_end, bool horizontal) {
    if (!tile->tap_overlay) return;

    lv_obj_t* ov = tile->tap_overlay;
    auto* ctx = new TapFlashCtx();
    ctx->overlay = ov;
    ctx->orig_x = lv_obj_get_x(ov);
    ctx->orig_y = lv_obj_get_y(ov);
    ctx->orig_w = lv_obj_get_width(ov);
    ctx->orig_h = lv_obj_get_height(ov);

    if (horizontal) {
        lv_obj_set_pos(ov, ctx->orig_x + px_start, ctx->orig_y);
        lv_obj_set_size(ov, px_end - px_start, ctx->orig_h);
    } else {
        lv_obj_set_pos(ov, ctx->orig_x, ctx->orig_y + px_start);
        lv_obj_set_size(ov, ctx->orig_w, px_end - px_start);
    }

    lv_obj_remove_flag(ov, LV_OBJ_FLAG_HIDDEN);
    lv_timer_t* t = lv_timer_create(PadScreen::tapFlashTimerCb, TAP_FLASH_DURATION_MS, ctx);
    if (!t) {
        lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(ov, ctx->orig_x, ctx->orig_y);
        lv_obj_set_size(ov, ctx->orig_w, ctx->orig_h);
        delete ctx;
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

    // Suppress taps that LVGL fires as part of a swipe gesture
    if (lv_tick_get() - swipe_actions_last_swipe_time() < 300) return;

    // Rocker widget: select zone-based action set from tap coordinates
    const ButtonAction* src_actions;
    uint8_t src_count;
    const char* event_label = "Tap";
    uint8_t flash_zone = 0;   // 0=full, 1=zone A (top/left), 2=zone B (bottom/right)
    bool rocker_horizontal = false;
    bool is_zone_b = false;

    if (tile->widget_type && strcmp(tile->widget_type->name, "rocker") == 0) {
        // Determine which zone was tapped (primary vs secondary)
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        lv_area_t area;
        lv_obj_get_coords(tile->obj, &area);

        // Read axis from widget config
        rocker_horizontal = tile->widget_cfg.data[0]; // RockerConfig.horizontal is the first byte

        if (rocker_horizontal) {
            int16_t mid_x = (area.x1 + area.x2) / 2;
            is_zone_b = (point.x > mid_x);
        } else {
            int16_t mid_y = (area.y1 + area.y2) / 2;
            is_zone_b = (point.y > mid_y);
        }

        if (is_zone_b) {
            src_actions = tile->lp_actions;
            src_count = tile->lp_action_count;
            event_label = rocker_horizontal ? "RockerR" : "RockerD";
            flash_zone = 2;
        } else {
            src_actions = tile->actions;
            src_count = tile->action_count;
            event_label = rocker_horizontal ? "RockerL" : "RockerU";
            flash_zone = 1;
        }
    } else if (tile->widget_type && strcmp(tile->widget_type->name, "numericrocker") == 0) {
        // Numeric rocker: outer/inner zones use adjust_action with {step} substitution;
        // center zone falls through to standard tap dispatch (actions[0-2]).
        auto* cfg = reinterpret_cast<const NumericRockerConfig*>(tile->widget_cfg.data);

        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        lv_area_t area;
        lv_obj_get_coords(tile->obj, &area);

        int span = cfg->horizontal ? (area.x2 - area.x1) : (area.y2 - area.y1);
        if (span <= 0) return;
        int rel = cfg->horizontal ? (point.x - area.x1) : (point.y - area.y1);

        NRZoneLayout z = nr_compute_zones(span, cfg->small_step, cfg->large_step);

        // For vertical orientation, invert sign: top = increase, bottom = decrease
        float sign = cfg->horizontal ? 1.0f : -1.0f;

        if (rel < z.inner_end || rel >= z.inner2_start) {
            // Outer or inner zone — dispatch adjust_action with {step}
            float step;
            int flash_start, flash_end;
            const char* nr_event;
            bool is_large = false;
            if (rel < z.outer_end) {
                step = sign * -cfg->large_step;
                flash_start = 0; flash_end = z.outer_end;
                nr_event = "NRockerOuterA";
                is_large = true;
            } else if (rel < z.inner_end) {
                step = sign * -cfg->small_step;
                flash_start = z.outer_end; flash_end = z.inner_end;
                nr_event = "NRockerInnerA";
            } else if (rel < z.outer2_start) {
                step = sign * cfg->small_step;
                flash_start = z.inner2_start; flash_end = z.outer2_start;
                nr_event = "NRockerInnerB";
            } else {
                step = sign * cfg->large_step;
                flash_start = z.outer2_start; flash_end = span;
                nr_event = "NRockerOuterB";
                is_large = true;
            }

            if (cfg->adjust_action.type[0] == '\0') return;

            ButtonAction local_nr;
            memcpy(&local_nr, &cfg->adjust_action, sizeof(ButtonAction));
            numericrocker_substitute_step(&local_nr, step);

            do_tap_flash_px(tile, flash_start, flash_end, cfg->horizontal);
#if HAS_AUDIO
            if (!has_audio_action(&local_nr, 1)) {
                const char* pattern = is_large ? device_config.lp_beep : device_config.tap_beep;
                if (pattern[0] && strcmp(pattern, "none") != 0) {
                    audio_beep(pattern, 0);
                }
            }
#endif
            action_dispatch(local_nr, nr_event);
#if HAS_MQTT
            publish_button_event(tile, "press");
#endif
            return;
        }
        // Center zone — fall through to standard tap dispatch
        src_actions = tile->actions;
        src_count = tile->action_count;
    } else {
        src_actions = tile->actions;
        src_count = tile->action_count;
    }

    // Copy actions to local storage before dispatch — a screen nav action
    // may destroy this tile's owning PadScreen (LRU eviction).
    const uint8_t count = src_count;
    ButtonAction local[MAX_BUTTON_ACTIONS];
    memcpy(local, src_actions, count * sizeof(ButtonAction));

    // Visual and audio cues only when at least one action is configured
    if (has_any_action(local, count)) {
        do_tap_flash(tile, flash_zone, rocker_horizontal);
#if HAS_AUDIO
        if (!has_audio_action(local, count)) {
            const char* pattern = device_config.tap_beep;
            if (pattern[0] && strcmp(pattern, "none") != 0) {
                audio_beep(pattern, 0);
            }
        }
#endif
    }

    for (uint8_t i = 0; i < count; i++) {
        action_dispatch(local[i], event_label);
    }

#if HAS_MQTT
    publish_button_event(tile, "press");
#endif
}

void PadScreen::onLongPress(lv_event_t* e) {
    ButtonTile* tile = (ButtonTile*)lv_event_get_user_data(e);
    if (!tile || !tile->obj) return;

    // Rocker use tap zones — suppress long-press
    if (tile->widget_type &&
        strcmp(tile->widget_type->name, "rocker") == 0) return;

    // Numeric rocker: suppress long-press on outer/inner zones, allow center
    if (tile->widget_type &&
        strcmp(tile->widget_type->name, "numericrocker") == 0) {
        auto* cfg = reinterpret_cast<const NumericRockerConfig*>(tile->widget_cfg.data);
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        lv_area_t area;
        lv_obj_get_coords(tile->obj, &area);
        int span = cfg->horizontal ? (area.x2 - area.x1) : (area.y2 - area.y1);
        if (span > 0) {
            int rel = cfg->horizontal ? (point.x - area.x1) : (point.y - area.y1);
            NRZoneLayout z = nr_compute_zones(span, cfg->small_step, cfg->large_step);
            if (rel < z.inner_end || rel >= z.inner2_start) return; // outer/inner zone
        }
        // Center zone — fall through to standard LP dispatch
    }

    // Suppress long-press that LVGL fires as part of a swipe gesture
    if (lv_tick_get() - swipe_actions_last_swipe_time() < 300) return;

    // Copy actions to local storage before dispatch — a screen nav action
    // may destroy this tile's owning PadScreen (LRU eviction).
    const uint8_t count = tile->lp_action_count;
    ButtonAction local[MAX_BUTTON_ACTIONS];
    memcpy(local, tile->lp_actions, count * sizeof(ButtonAction));

    // Visual and audio cues only when at least one action is configured
    if (has_any_action(local, count)) {
        do_tap_flash(tile);
#if HAS_AUDIO
        if (!has_audio_action(local, count)) {
            const char* pattern = device_config.lp_beep;
            if (pattern[0] && strcmp(pattern, "none") != 0) {
                audio_beep(pattern, 0);
            }
        }
#endif
    }

    for (uint8_t i = 0; i < count; i++) {
        action_dispatch(local[i], "LP");
    }

#if HAS_MQTT
    publish_button_event(tile, "hold");
#endif
}

