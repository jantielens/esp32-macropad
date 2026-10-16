#include "pad_screen.h"
#include "../widgets/numericrocker_zones.h"
#include <cstring>
#if HAS_DISPLAY
bool PadScreen::navigationZoneAvailable(const ButtonTile& tile, uint8_t zone) const {
    if (!navigationZoneCount(tile)) return false;
    if (!tile.widget_type) return tile.action_count || tile.lp_action_count;
    if (strcmp(tile.widget_type->name, "rocker") == 0) return zone ? tile.lp_action_count : tile.action_count;
    if (strcmp(tile.widget_type->name, "numericrocker") == 0 && navigationZoneCount(tile) == 5) {
        const auto* config = reinterpret_cast<const NumericRockerConfig*>(tile.widget_cfg.data);
        if (zone == 2) return tile.action_count || tile.lp_action_count;
        if (!(zone == 0 || zone == 4 ? config->large_step > 0 : config->small_step > 0)) return false;
        const lv_area_t area = navigationArea(tile, zone);
        return area.x2 >= area.x1 && area.y2 >= area.y1;
    }
    return tile.action_count || tile.lp_action_count;
}

uint8_t PadScreen::navigationZoneCount(const ButtonTile& tile) const {
    if (padActionOverlay && padActionCount) return 0;
    if (!tile.obj || lv_obj_has_flag(tile.obj, LV_OBJ_FLAG_HIDDEN) ||
        lv_obj_has_state(tile.obj, LV_STATE_DISABLED)) return 0;
    if (tile.widget_type && strcmp(tile.widget_type->name, "numericrocker") == 0) {
        const auto* config = reinterpret_cast<const NumericRockerConfig*>(tile.widget_cfg.data);
        if (config->adjust_action.type[0] && (config->small_step > 0 || config->large_step > 0)) return 5;
    }
    if (!tile.action_count && !tile.lp_action_count) return 0;
    return tile.widget_type && strcmp(tile.widget_type->name, "rocker") == 0 ? 2 : 1;
}

lv_area_t PadScreen::navigationArea(const ButtonTile& tile, uint8_t zone) const {
    lv_area_t area;
    lv_obj_get_coords(tile.obj, &area);
    if (!tile.widget_type) return area;
    if (strcmp(tile.widget_type->name, "rocker") == 0) {
        if (tile.widget_cfg.data[0]) {
            const int middle = (area.x1 + area.x2) / 2;
            if (zone) area.x1 = middle + 1;
            else area.x2 = middle;
        } else {
            const int middle = (area.y1 + area.y2) / 2;
            if (zone) area.y1 = middle + 1;
            else area.y2 = middle;
        }
    } else if (strcmp(tile.widget_type->name, "numericrocker") == 0 && navigationZoneCount(tile) == 5) {
        const auto* config = reinterpret_cast<const NumericRockerConfig*>(tile.widget_cfg.data);
        lv_obj_get_content_coords(tile.obj, &area);
        const int span = config->horizontal ? area.x2 - area.x1 + 1 : area.y2 - area.y1 + 1;
        const NRZoneLayout layout = nr_compute_zones(span, config->small_step, config->large_step, config->zone_scale_pct);
        const int bounds[] = {0, layout.outer_end, layout.inner_end, layout.inner2_start, layout.outer2_start, span};
        if (config->horizontal) { area.x2 = area.x1 + bounds[zone + 1] - 1; area.x1 += bounds[zone]; }
        else { area.y2 = area.y1 + bounds[zone + 1] - 1; area.y1 += bounds[zone]; }
    }
    return area;
}

void PadScreen::refreshNavigationFocus() {
    if (navigationTile == -1) {
        if (navigationFocus) lv_obj_add_flag(navigationFocus, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const bool padActionActive = padActionOverlay && padActionCount;
    lv_obj_update_layout(container);
    const bool selectedAvailable = padActionActive
        ? navigationTile == NAVIGATION_PAD_ACTION
        : tiles && navigationTile >= 0 && navigationTile < tileCount &&
            navigationZoneAvailable(tiles[navigationTile], navigationZone);
    if (!selectedAvailable) {
        navigationTile = -1;
        navigationZone = 0;
        if (navigationFocus) lv_obj_add_flag(navigationFocus, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!navigationFocus) {
        navigationFocus = lv_obj_create(container);
        lv_obj_remove_style_all(navigationFocus);
        lv_obj_remove_flag(navigationFocus, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(navigationFocus, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(navigationFocus, LV_OPA_TRANSP, 0);
    }
    lv_obj_t* appearance = padActionActive ? container : tiles[navigationTile].obj;
    const int32_t borderWidth = lv_obj_get_style_border_width(appearance, LV_PART_MAIN);
    lv_obj_set_style_border_width(navigationFocus, borderWidth > 1 ? borderWidth * 2 : 2, 0);
    lv_obj_set_style_border_color(navigationFocus, borderWidth > 0
        ? lv_obj_get_style_border_color(appearance, LV_PART_MAIN)
        : lv_obj_get_style_text_color(appearance, LV_PART_MAIN), 0);
    lv_obj_set_style_border_opa(navigationFocus, borderWidth > 0
        ? lv_obj_get_style_border_opa(appearance, LV_PART_MAIN) : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(navigationFocus, lv_obj_get_style_radius(appearance, LV_PART_MAIN), 0);
    lv_obj_update_layout(container);
    lv_area_t area;
    if (padActionActive) lv_obj_get_coords(padActionOverlay, &area);
    else area = navigationArea(tiles[navigationTile], navigationZone);
    lv_area_t origin;
    lv_obj_get_coords(container, &origin);
    lv_obj_set_pos(navigationFocus, area.x1 - origin.x1, area.y1 - origin.y1);
    lv_obj_set_size(navigationFocus, area.x2 - area.x1 + 1, area.y2 - area.y1 + 1);
    lv_obj_remove_flag(navigationFocus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(navigationFocus);
}

bool PadScreen::navigateButtons(const char* command) {
    update();
    struct Target { int16_t tile; uint8_t zone; };
    Target targets[MAX_PAD_BUTTONS * 5 + 1];
    size_t count = 0;
    int selected = -1;
    if (padActionOverlay && padActionCount) {
        targets[count++] = {NAVIGATION_PAD_ACTION, 0};
        if (navigationTile == NAVIGATION_PAD_ACTION) selected = 0;
    }
    for (uint8_t index = 0; index < tileCount; ++index) {
        for (uint8_t zone = 0; zone < navigationZoneCount(tiles[index]); ++zone) {
            if (!navigationZoneAvailable(tiles[index], zone)) continue;
            if (index == navigationTile && zone == navigationZone) selected = count;
            targets[count++] = {index, zone};
        }
    }
    if (!count) return false;
    if (strcmp(command, "previous") == 0 || strcmp(command, "next") == 0) {
        const bool previous = strcmp(command, "previous") == 0;
        selected = selected < 0 ? (previous ? count - 1 : 0)
            : (previous ? (selected + count - 1) % count : (selected + 1) % count);
        navigationTile = targets[selected].tile;
        navigationZone = targets[selected].zone;
        refreshNavigationFocus();
        return true;
    }
    if (strcmp(command, "tap") != 0 && strcmp(command, "tap_hold") != 0) return false;
    if (selected < 0) return true;
    if (targets[selected].tile == NAVIGATION_PAD_ACTION) {
        if (strcmp(command, "tap") == 0) lv_obj_send_event(padActionOverlay, LV_EVENT_CLICKED, nullptr);
        return true;
    }
    lv_obj_update_layout(container);
    ButtonTile* tile = &tiles[targets[selected].tile];
    const lv_area_t area = navigationArea(*tile, targets[selected].zone);
    const lv_point_t point = {(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};
    if (strcmp(command, "tap_hold") == 0) longPressAt(tile, point);
    else tapAt(tile, point);
    return true;
}
#endif
#include "pad_screen.h"
#include "../action_list.h"
#include "../device_dialog.h"
#include "../display_manager.h"
#include "../log_manager.h"
#include "../swipe_actions.h"
#if HAS_NATIVE_EXTENSIONS
#include "../native_extension.h"
#include "../widgets/external_widget.h"
#endif
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
    return action_list_has_audio(acts, count);
}

// ============================================================================
// Event Handlers
// ============================================================================

// Tap flash: show overlay briefly, then hide after timeout.
// For rocker widgets, the overlay is resized to cover only the tapped zone;
// on timeout we restore full size so the next flash starts clean.
static void restore_tap_overlay(ButtonTile* tile) {
    if (!tile->tap_overlay) return;
    lv_obj_set_pos(tile->tap_overlay, tile->tap_flash_x, tile->tap_flash_y);
    lv_obj_set_size(tile->tap_overlay, tile->tap_flash_w, tile->tap_flash_h);
}

void PadScreen::tapFlashTimerCb(lv_timer_t* timer) {
    auto* tile = (ButtonTile*)lv_timer_get_user_data(timer);
    if (tile) {
        tile->tap_flash_timer = nullptr;
        if (tile->tap_overlay) {
            lv_obj_add_flag(tile->tap_overlay, LV_OBJ_FLAG_HIDDEN);
            restore_tap_overlay(tile);
        }
    }
    lv_timer_delete(timer);
}

// zone: 0 = full button, 1 = zone A (top/left), 2 = zone B (bottom/right)
// horizontal: only meaningful when zone != 0
static void do_tap_flash(ButtonTile* tile, uint8_t zone = 0, bool horizontal = false) {
    if (DISPLAY_DISABLE_ANIMATIONS) return;
    if (!tile->tap_overlay) return;

    lv_obj_t* ov = tile->tap_overlay;
    if (tile->tap_flash_timer) {
        lv_timer_delete(tile->tap_flash_timer);
        tile->tap_flash_timer = nullptr;
        restore_tap_overlay(tile);
    }
    tile->tap_flash_x = lv_obj_get_x(ov);
    tile->tap_flash_y = lv_obj_get_y(ov);
    tile->tap_flash_w = lv_obj_get_width(ov);
    tile->tap_flash_h = lv_obj_get_height(ov);

    // Resize to cover only the tapped zone for rocker
    if (zone == 1) {
        // Zone A: top half (vertical) or left half (horizontal)
        if (horizontal) {
            lv_obj_set_size(ov, tile->tap_flash_w / 2, tile->tap_flash_h);
        } else {
            lv_obj_set_size(ov, tile->tap_flash_w, tile->tap_flash_h / 2);
        }
    } else if (zone == 2) {
        // Zone B: bottom half (vertical) or right half (horizontal)
        if (horizontal) {
            lv_obj_set_pos(ov, tile->tap_flash_x + tile->tap_flash_w / 2, tile->tap_flash_y);
            lv_obj_set_size(ov, tile->tap_flash_w - tile->tap_flash_w / 2, tile->tap_flash_h);
        } else {
            lv_obj_set_pos(ov, tile->tap_flash_x, tile->tap_flash_y + tile->tap_flash_h / 2);
            lv_obj_set_size(ov, tile->tap_flash_w, tile->tap_flash_h - tile->tap_flash_h / 2);
        }
    }

    lv_obj_remove_flag(ov, LV_OBJ_FLAG_HIDDEN);
    tile->tap_flash_timer = lv_timer_create(PadScreen::tapFlashTimerCb,
                                             TAP_FLASH_DURATION_MS, tile);
    if (!tile->tap_flash_timer) {
        lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
        restore_tap_overlay(tile);
    }
}

void PadScreen::onWidgetPressFeedback(lv_event_t* event) {
    if (DISPLAY_DISABLE_ANIMATIONS) return;
    auto* tile = static_cast<ButtonTile*>(lv_event_get_user_data(event));
    const auto* feedback = static_cast<const lv_event_code_t*>(lv_event_get_param(event));
    if (!tile || !tile->tap_overlay || !feedback) return;
    if (*feedback == LV_EVENT_RELEASED) {
        do_tap_flash(tile);
        return;
    }
    if (tile->tap_flash_timer) {
        lv_timer_delete(tile->tap_flash_timer);
        tile->tap_flash_timer = nullptr;
        restore_tap_overlay(tile);
    }
    if (*feedback == LV_EVENT_PRESSED) lv_obj_remove_flag(tile->tap_overlay, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(tile->tap_overlay, LV_OBJ_FLAG_HIDDEN);
}

// Pixel-based tap flash: resize overlay to a pixel band of the tile.
// px_start/px_end are tile-relative pixel offsets along the given axis.
static void do_tap_flash_px(ButtonTile* tile, int px_start, int px_end, bool horizontal) {
    if (DISPLAY_DISABLE_ANIMATIONS) return;
    if (!tile->tap_overlay) return;

    lv_obj_t* ov = tile->tap_overlay;
    if (tile->tap_flash_timer) {
        lv_timer_delete(tile->tap_flash_timer);
        tile->tap_flash_timer = nullptr;
        restore_tap_overlay(tile);
    }
    tile->tap_flash_x = lv_obj_get_x(ov);
    tile->tap_flash_y = lv_obj_get_y(ov);
    tile->tap_flash_w = lv_obj_get_width(ov);
    tile->tap_flash_h = lv_obj_get_height(ov);

    if (horizontal) {
        lv_obj_set_pos(ov, tile->tap_flash_x + px_start, tile->tap_flash_y);
        lv_obj_set_size(ov, px_end - px_start, tile->tap_flash_h);
    } else {
        lv_obj_set_pos(ov, tile->tap_flash_x, tile->tap_flash_y + px_start);
        lv_obj_set_size(ov, tile->tap_flash_w, px_end - px_start);
    }

    lv_obj_remove_flag(ov, LV_OBJ_FLAG_HIDDEN);
    tile->tap_flash_timer = lv_timer_create(PadScreen::tapFlashTimerCb,
                                             TAP_FLASH_DURATION_MS, tile);
    if (!tile->tap_flash_timer) {
        lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
        restore_tap_overlay(tile);
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

void PadScreen::onPress(lv_event_t* e) {
    ButtonTile* tile = (ButtonTile*)lv_event_get_user_data(e);
    if (!tile) return;

    lv_indev_t* indev = lv_indev_active();
    if (!indev) {
        tile->touch_start_valid = false;
        return;
    }
    lv_indev_get_point(indev, &tile->touch_start);
    tile->touch_start_valid = true;
}

void PadScreen::onTap(lv_event_t* e) {
    ButtonTile* tile = (ButtonTile*)lv_event_get_user_data(e);
    if (!tile || !tile->obj) return;
    lv_point_t point = {};
    if (tile->widget_type && strcmp(tile->widget_type->name, "numericrocker") == 0 &&
        tile->touch_start_valid) point = tile->touch_start;
    else if (lv_indev_active()) lv_indev_get_point(lv_indev_active(), &point);
    tapAt(tile, point);
}

void PadScreen::tapAt(ButtonTile* tile, const lv_point_t& point) {
    if (!tile || !tile->obj) return;
    tile->touch_start_valid = false;
    if (device_dialog_active()) return;

    // Suppress taps that LVGL fires as part of a swipe gesture
    if (lv_tick_get() - swipe_actions_last_swipe_time() < 300) return;

#if HAS_NATIVE_EXTENSIONS
    if (strcmp(tile->widget_cfg.type, "external") == 0) {
        const uint32_t instance_id = external_widget_instance_id(tile->page, tile->col, tile->row);
        if (native_extension_on_tap(external_widget_config(&tile->widget_cfg)->extension_id, instance_id) == NATIVE_EXTENSION_HANDLED) return;
    }
#endif

    // Rocker widget: select zone-based action set from tap coordinates
    const ButtonAction* src_actions;
    uint8_t src_count;
    const char* event_label = "Tap";
    uint8_t flash_zone = 0;   // 0=full, 1=zone A (top/left), 2=zone B (bottom/right)
    bool rocker_horizontal = false;
    bool is_zone_b = false;

    if (tile->widget_type && strcmp(tile->widget_type->name, "rocker") == 0) {
        // Determine which zone was tapped (primary vs secondary)
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

        lv_area_t content;
        lv_area_t tile_area;
        lv_obj_get_content_coords(tile->obj, &content);
        lv_obj_get_coords(tile->obj, &tile_area);
        int span = cfg->horizontal ? (content.x2 - content.x1 + 1)
                       : (content.y2 - content.y1 + 1);
        if (span <= 0) return;
        int rel = cfg->horizontal ? (point.x - content.x1) : (point.y - content.y1);
        rel = nr_clamp(rel, 0, span - 1);
        int axis_offset = cfg->horizontal ? (content.x1 - tile_area.x1)
                          : (content.y1 - tile_area.y1);

        NRZoneLayout z = nr_compute_zones(span, cfg->small_step, cfg->large_step,
                          cfg->zone_scale_pct);

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

            do_tap_flash_px(tile, axis_offset + flash_start, axis_offset + flash_end,
                            cfg->horizontal);
#if HAS_AUDIO
            if (!has_audio_action(&local_nr, 1)) {
                const char* pattern = is_large ? device_config.lp_beep : device_config.tap_beep;
                if (pattern[0] && strcmp(pattern, "none") != 0) {
                    audio_feedback(pattern);
                }
            }
#endif
            action_list_dispatch(&local_nr, 1, nr_event, ACTION_CONTINUATION_OWNER_LVGL);
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
                audio_feedback(pattern);
            }
        }
#endif
    }

    if (tile->widget_type) {
        action_list_dispatch(local, count, event_label, ACTION_CONTINUATION_OWNER_LVGL);
    } else {
        if (!device_dialog_active())
            action_list_dispatch(local, count, event_label, ACTION_CONTINUATION_OWNER_LVGL);
    }

#if HAS_MQTT
    publish_button_event(tile, "press");
#endif
}

void PadScreen::onLongPress(lv_event_t* e) {
    ButtonTile* tile = (ButtonTile*)lv_event_get_user_data(e);
    if (!tile || !tile->obj) return;
    lv_point_t point = {};
    if (lv_indev_active()) lv_indev_get_point(lv_indev_active(), &point);
    longPressAt(tile, point);
}

void PadScreen::longPressAt(ButtonTile* tile, const lv_point_t& point) {
    if (!tile || !tile->obj) return;
    if (device_dialog_active()) return;
    (void)point;

#if HAS_NATIVE_EXTENSIONS
    if (strcmp(tile->widget_cfg.type, "external") == 0) {
        const uint32_t instance_id = external_widget_instance_id(tile->page, tile->col, tile->row);
        if (native_extension_on_long_press(external_widget_config(&tile->widget_cfg)->extension_id, instance_id) == NATIVE_EXTENSION_HANDLED) return;
    }
#endif

    // Rocker use tap zones — suppress long-press
    if (tile->widget_type &&
        strcmp(tile->widget_type->name, "rocker") == 0) return;

    // Numeric rocker: suppress long-press on outer/inner zones, allow center
    if (tile->widget_type &&
        strcmp(tile->widget_type->name, "numericrocker") == 0) {
        auto* cfg = reinterpret_cast<const NumericRockerConfig*>(tile->widget_cfg.data);
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
                audio_feedback(pattern);
            }
        }
#endif
    }

    if (tile->widget_type) {
        action_list_dispatch(local, count, "LP", ACTION_CONTINUATION_OWNER_LVGL);
    } else {
        if (!device_dialog_active())
            action_list_dispatch(local, count, "LP", ACTION_CONTINUATION_OWNER_LVGL);
    }

#if HAS_MQTT
    publish_button_event(tile, "hold");
#endif
}

void PadScreen::clearPadActionOverlay() {
    if (padActionFlashTimer) {
        lv_timer_delete(padActionFlashTimer);
        padActionFlashTimer = nullptr;
    }
    if (padActionOverlay) {
        lv_obj_delete(padActionOverlay);
        padActionOverlay = nullptr;
    }
}

void PadScreen::padActionFlashTimerCb(lv_timer_t* timer) {
    auto* pad = (PadScreen*)lv_timer_get_user_data(timer);
    if (pad) {
        pad->padActionFlashTimer = nullptr;
        if (pad->padActionOverlay) {
            lv_obj_set_style_bg_opa(pad->padActionOverlay, LV_OPA_TRANSP, 0);
        }
    }
    lv_timer_delete(timer);
}

void PadScreen::showPadActionFlash() {
    if (DISPLAY_DISABLE_ANIMATIONS) return;
    if (!padActionOverlay) return;
    if (padActionFlashTimer) {
        lv_timer_delete(padActionFlashTimer);
        padActionFlashTimer = nullptr;
    }

    bool is_light = perceived_luminance(pageBgDefault) > TAP_LUMINANCE_THRESH;
    lv_obj_set_style_bg_color(padActionOverlay,
                              is_light ? lv_color_black() : lv_color_white(), 0);
    lv_obj_set_style_bg_opa(padActionOverlay,
                            is_light ? TAP_OVERLAY_DARK_OPA : TAP_OVERLAY_LIGHT_OPA, 0);
    padActionFlashTimer = lv_timer_create(padActionFlashTimerCb,
                                          TAP_FLASH_DURATION_MS, this);
    if (!padActionFlashTimer) {
        lv_obj_set_style_bg_opa(padActionOverlay, LV_OPA_TRANSP, 0);
    }
}

void PadScreen::onPadActionTap(lv_event_t* e) {
    if (device_dialog_active()) return;
    auto* pad = (PadScreen*)lv_event_get_user_data(e);
    if (!pad || !pad->padActions || pad->padActionCount == 0) return;
    if (lv_tick_get() - swipe_actions_last_swipe_time() < 300) return;

    ButtonAction local[MAX_BUTTON_ACTIONS];
    const uint8_t count = pad->padActionCount;
    memcpy(local, pad->padActions, count * sizeof(ButtonAction));

    pad->showPadActionFlash();
#if HAS_AUDIO
    if (!has_audio_action(local, count)) {
        const char* pattern = device_config.tap_beep;
        if (pattern[0] && strcmp(pattern, "none") != 0) {
            audio_feedback(pattern);
        }
    }
#endif
    action_list_dispatch(local, count, "PadTap", ACTION_CONTINUATION_OWNER_LVGL);
}
