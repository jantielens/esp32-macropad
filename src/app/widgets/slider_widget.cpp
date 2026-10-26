#include "widget.h"

#if HAS_DISPLAY && HAS_TOUCH

#include "slider_widget.h"
#include "slider_numeric.h"
#include "../action_list.h"
#include "../action_parse.h"
#include "../action_registry.h"
#include "../action_validate.h"
#include <algorithm>
#include <new>
#include <string.h>

namespace {

struct SliderConfig {
    SliderNumericRange range;
    char track_color[8];
    char fill_color[8];
    char knob_color[8];
    uint8_t axis;
    uint8_t length_pct;
    uint8_t icon_position;
    uint8_t track_px;
    uint8_t knob_px;
    bool valid_range;
};

struct SliderState {
    const SliderConfig* config;
    const ButtonAction* actions;
    uint8_t action_count;
    lv_obj_t* button;
    lv_obj_t* control;
    lv_obj_t* icon;
    lv_obj_t* center_label;
    lv_obj_t* top_label;
    lv_obj_t* bottom_label;
    uint16_t display_position;
    uint16_t reported_position;
    uint16_t draft_position;
    uint16_t gesture_start_position;
    uint16_t pending_position;
    uint32_t pending_since;
    int16_t offset_x;
    int16_t offset_y;
    int16_t label_offset_x;
    int16_t label_offset_y;
    uint16_t icon_scale_x;
    uint16_t icon_scale_y;
    bool bound;
    bool available;
    bool has_report;
    bool pending;
    bool gesture_active;
    bool programmatic_update;
    bool layout_dirty;
    bool layout_usable;
};

static_assert(sizeof(SliderConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "SliderConfig exceeds WIDGET_CONFIG_MAX_BYTES");
static_assert(sizeof(SliderState) <= WIDGET_STATE_MAX_BYTES,
              "SliderState exceeds WIDGET_STATE_MAX_BYTES");

static const char* const kSliderFields[] = {
    "widget_slider_min", "widget_slider_max", "widget_slider_step",
    "widget_slider_axis", "widget_slider_length_pct", "widget_slider_track_px",
    "widget_slider_knob_px", "widget_slider_track_color", "widget_slider_fill_color",
    "widget_slider_knob_color", "widget_slider_icon_position"
};

static bool slider_known_field(const char* name) {
    for (const char* field : kSliderFields) if (strcmp(field, name) == 0) return true;
    return false;
}

static bool slider_color_valid(JsonObjectConst button, const char* name) {
    if (!button.containsKey(name)) return true;
    if (!button[name].is<const char*>()) return false;
    const char* value = button[name].as<const char*>();
    uint32_t rgb;
    return strlen(value) == 7 && value[0] == '#' && parse_hex_color(value, &rgb);
}

static bool slider_number(JsonObjectConst button, const char* name, double fallback,
                          double* output) {
    if (!button.containsKey(name)) {
        *output = fallback;
        return true;
    }
    if (!button[name].is<double>()) return false;
    *output = button[name].as<double>();
    return std::isfinite(*output);
}

static bool slider_has_ordinary_actions(JsonObjectConst button, const char* name) {
    if (!button.containsKey(name) || button[name].isNull()) return false;
    if (!button[name].is<JsonArrayConst>()) return true;
    return button[name].as<JsonArrayConst>().size() != 0;
}

static const char* slider_validate(JsonObjectConst button) {
    for (JsonPairConst pair : button) {
        const char* name = pair.key().c_str();
        if (strncmp(name, "widget_slider_", 14) == 0 && !slider_known_field(name))
            return "unsupported slider field";
    }
    double minimum, maximum, step;
    SliderNumericRange range{};
    if (!slider_number(button, "widget_slider_min", 0, &minimum)
            || !slider_number(button, "widget_slider_max", 100, &maximum)
            || !slider_number(button, "widget_slider_step", 1, &step)
            || !slider_numeric_range_init(minimum, maximum, step, &range))
        return "slider range requires finite endpoints and a positive exact step with at most 10000 intervals";
    if (button.containsKey("widget_slider_axis")) {
        if (!button["widget_slider_axis"].is<const char*>()) return "slider axis must be horizontal or vertical";
        const char* axis = button["widget_slider_axis"].as<const char*>();
        if (strcmp(axis, "horizontal") && strcmp(axis, "vertical"))
            return "slider axis must be horizontal or vertical";
    }
    if (button.containsKey("widget_slider_length_pct") &&
        (!button["widget_slider_length_pct"].is<int>() ||
         button["widget_slider_length_pct"].as<int>() < 20 ||
         button["widget_slider_length_pct"].as<int>() > 100))
        return "slider length must be an integer from 20 to 100";
    if (button.containsKey("widget_slider_track_px") &&
        (!button["widget_slider_track_px"].is<int>() ||
         button["widget_slider_track_px"].as<int>() < 0 ||
         button["widget_slider_track_px"].as<int>() > 32))
        return "slider track size must be 0 or an integer from 1 to 32";
    if (button.containsKey("widget_slider_knob_px") &&
        (!button["widget_slider_knob_px"].is<int>() ||
         button["widget_slider_knob_px"].as<int>() < 0 ||
         button["widget_slider_knob_px"].as<int>() > 64 ||
         (button["widget_slider_knob_px"].as<int>() > 0 &&
          button["widget_slider_knob_px"].as<int>() < 8)))
        return "slider knob size must be 0 or an integer from 8 to 64";
    if (!slider_color_valid(button, "widget_slider_track_color") ||
        !slider_color_valid(button, "widget_slider_fill_color") ||
        !slider_color_valid(button, "widget_slider_knob_color"))
        return "slider colors must be #RRGGBB literals";
    if (button.containsKey("widget_data_binding") && !button["widget_data_binding"].is<const char*>())
        return "slider source must be a string";
    const char* icon_position = button["widget_slider_icon_position"] | "left";
    if (strcmp(icon_position, "left") && strcmp(icon_position, "above") && strcmp(icon_position, "below"))
        return "slider icon position must be left, above, or below";
    if (slider_has_ordinary_actions(button, "lp_actions")) return "slider does not support long-press actions";
    return nullptr;
}

static void slider_parse(const JsonObject& button, uint8_t* data) {
    auto* config = new (data) SliderConfig{};
    const double minimum = button["widget_slider_min"] | 0.0;
    const double maximum = button["widget_slider_max"] | 100.0;
    const double step = button["widget_slider_step"] | 1.0;
    config->valid_range = slider_numeric_range_init(minimum, maximum, step, &config->range);
    const char* axis = button["widget_slider_axis"] | "horizontal";
    config->axis = strcmp(axis, "vertical") == 0 ? 1 : 0;
    config->length_pct = static_cast<uint8_t>(button["widget_slider_length_pct"] | 50);
    const char* icon_position = button["widget_slider_icon_position"] | "left";
    config->icon_position = !strcmp(icon_position, "above") ? 1 : !strcmp(icon_position, "below") ? 2 : 0;
    config->track_px = static_cast<uint8_t>(button["widget_slider_track_px"] | 0);
    config->knob_px = static_cast<uint8_t>(button["widget_slider_knob_px"] | 0);
    strlcpy(config->track_color, button["widget_slider_track_color"] | DEFAULT_WIDGET_TRACK_COLOR,
            sizeof(config->track_color));
    strlcpy(config->fill_color, button["widget_slider_fill_color"] | DEFAULT_WIDGET_SERIES_COLOR,
            sizeof(config->fill_color));
    strlcpy(config->knob_color, button["widget_slider_knob_color"] | DEFAULT_WIDGET_INDICATOR_COLOR,
            sizeof(config->knob_color));
}

static lv_color_t slider_color(const char* value, uint32_t fallback) {
    uint32_t rgb = fallback;
    parse_hex_color(value, &rgb);
    return lv_color_make((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

static void slider_render(SliderState* state) {
    if (!state->control) return;
    const bool usable = state->available && state->layout_usable;
    if (usable) lv_obj_clear_state(state->control, LV_STATE_DISABLED);
    else lv_obj_add_state(state->control, LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(state->control, usable ? LV_OPA_COVER : LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(state->control, usable && !state->pending ? LV_OPA_COVER : LV_OPA_50,
                            LV_PART_INDICATOR);
    if (usable) lv_obj_clear_flag(state->control, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(state->control, LV_OBJ_FLAG_HIDDEN);
}

static void slider_set_display(SliderState* state, uint16_t position) {
    if (position > state->config->range.intervals) position = state->config->range.intervals;
    state->display_position = position;
    if (state->control && lv_slider_get_value(state->control) != position) {
        state->programmatic_update = true;
        lv_slider_set_value(state->control, position, LV_ANIM_OFF);
        state->programmatic_update = false;
    }
    slider_render(state);
}

struct SliderContentGroup {
    lv_area_t icon_bounds;
    lv_area_t label_bounds;
    int icon_width;
    int icon_height;
    int label_width;
    int label_height;
    int gap;
    int height;
};

static SliderContentGroup slider_measure_group(SliderState* state, int width) {
    SliderContentGroup group{};
    if (width <= 0) {
        if (state->icon) lv_obj_add_flag(state->icon, LV_OBJ_FLAG_HIDDEN);
        if (state->center_label) lv_obj_add_flag(state->center_label, LV_OBJ_FLAG_HIDDEN);
        return group;
    }
    if (state->icon) lv_obj_remove_flag(state->icon, LV_OBJ_FLAG_HIDDEN);
    if (state->center_label) lv_obj_remove_flag(state->center_label, LV_OBJ_FLAG_HIDDEN);
    if (state->center_label) {
        lv_obj_set_width(state->center_label, width);
        lv_obj_set_height(state->center_label, lv_font_get_line_height(lv_obj_get_style_text_font(state->center_label, LV_PART_MAIN)));
        lv_label_set_long_mode(state->center_label, LV_LABEL_LONG_DOT);
        lv_obj_update_layout(state->button);
    }
    group.icon_bounds = widget_content_bounds(state->icon);
    group.label_bounds = widget_content_bounds(state->center_label);
    group.icon_width = state->icon ? lv_area_get_width(&group.icon_bounds) : 0;
    group.icon_height = state->icon ? lv_area_get_height(&group.icon_bounds) : 0;
    if (state->icon && group.icon_width > width) {
        lv_obj_add_flag(state->icon, LV_OBJ_FLAG_HIDDEN);
        group.icon_width = 0;
        group.icon_height = 0;
    }
    group.label_width = state->center_label ? lv_area_get_width(&group.label_bounds) : 0;
    group.label_height = state->center_label ? lv_area_get_height(&group.label_bounds) : 0;
    group.gap = group.icon_height && group.label_height ? 4 : 0;
    group.height = group.icon_height + group.gap + group.label_height;
    return group;
}

static void slider_hide_content_group(SliderState* state) {
    if (state->icon) lv_obj_add_flag(state->icon, LV_OBJ_FLAG_HIDDEN);
    if (state->center_label) lv_obj_add_flag(state->center_label, LV_OBJ_FLAG_HIDDEN);
}

static void slider_place_group(SliderState* state, int x, int y, int width,
                               const SliderContentGroup& group) {
    if (group.height <= 0 || width <= 0) return;
    x += state->offset_x;
    y += state->offset_y;
    int cursor_y = y;
    if (state->icon && group.icon_height) {
        lv_obj_set_pos(state->icon,
                       x + (width - group.icon_width) / 2 - group.icon_bounds.x1,
                       cursor_y - group.icon_bounds.y1);
        cursor_y += group.icon_height + group.gap;
    }
    if (state->center_label) {
        lv_obj_set_pos(state->center_label,
                       x + (width - group.label_width) / 2 - group.label_bounds.x1
                           + state->label_offset_x,
                       cursor_y - group.label_bounds.y1 + state->label_offset_y);
    }
}

static void slider_layout(SliderState* state) {
    if (!state->button || !state->control) return;
    const SliderConfig& config = *state->config;
    lv_obj_update_layout(state->button);
    const int content_width = lv_obj_get_content_width(state->button);
    int body_top = 0;
    int body_bottom = lv_obj_get_content_height(state->button);
    if (state->top_label) {
        const lv_area_t bounds = widget_content_bounds(state->top_label);
        const int label_bottom = lv_obj_get_y(state->top_label) + bounds.y2 + 1;
        if (label_bottom + 4 > body_top) body_top = label_bottom + 4;
    }
    if (state->bottom_label) {
        const lv_area_t bounds = widget_content_bounds(state->bottom_label);
        const int label_top = lv_obj_get_y(state->bottom_label) + bounds.y1;
        if (label_top - 4 < body_bottom) body_bottom = label_top - 4;
    }
    if (state->icon) lv_obj_remove_flag(state->icon, LV_OBJ_FLAG_HIDDEN);
    if (state->center_label) lv_obj_remove_flag(state->center_label, LV_OBJ_FLAG_HIDDEN);
    if (state->icon && lv_obj_check_type(state->icon, &lv_image_class)) {
        lv_image_set_scale_x(state->icon, state->icon_scale_x);
        lv_image_set_scale_y(state->icon, state->icon_scale_y);
        const lv_area_t bounds = widget_content_bounds(state->icon);
        const int max_width = content_width * 4 / 10;
        const int max_height = (body_bottom - body_top) * 4 / 10;
        const int width_scale = max_width * 256 / lv_area_get_width(&bounds);
        const int height_scale = max_height * 256 / lv_area_get_height(&bounds);
        const int scale = clamp_val<int>(width_scale < height_scale ? width_scale : height_scale, 1, 256);
        lv_image_set_scale_x(state->icon, state->icon_scale_x * scale / 256);
        lv_image_set_scale_y(state->icon, state->icon_scale_y * scale / 256);
    }
    const lv_area_t icon_bounds = widget_content_bounds(state->icon);
    const int icon_width = state->icon ? lv_area_get_width(&icon_bounds) : 0;
    const int icon_height = state->icon ? lv_area_get_height(&icon_bounds) : 0;
    const bool side_icon = config.icon_position == 0;
    const int outer_icon_height = state->icon && !side_icon ? icon_height + 4 : 0;
    const int full_top = body_top;
    const int full_bottom = body_bottom;
    if (config.icon_position == 1) body_top += outer_icon_height;
    if (config.icon_position == 2) body_bottom -= outer_icon_height;
    const int body_height = body_bottom > body_top ? body_bottom - body_top : 0;
    const bool shared_row = (state->icon && side_icon) || state->center_label;
    const int target_length = (config.axis == 0 ? content_width : body_height)
        * (shared_row ? config.length_pct : 100) / 100;
    const int cross_available = config.axis == 0 ? body_height : content_width;
    const int cross_limit = cross_available > 0 ? cross_available : 1;
    const int automatic_knob = std::min(50, std::min(cross_available * 3 / 5, target_length / 4));
    const int requested_knob = clamp_val<int>(config.knob_px ? config.knob_px : automatic_knob,
                                              1, cross_limit);
    const int track = clamp_val<int>(config.track_px ? config.track_px : requested_knob * 2 / 5,
                                     1, cross_limit);
    const int knob = clamp_val<int>(requested_knob, track, cross_limit);
    bool usable = config.valid_range && content_width > 0 && body_height > 0
        && cross_available >= 8 && knob >= 8;
    const int control_length = target_length > knob ? target_length - knob : 1;
    usable = usable && control_length >= knob && control_length > track;
    int control_x = 0, control_y = body_top;
    if (config.axis == 0) {
        const int slot_left = shared_row ? content_width - target_length : (content_width - target_length) / 2;
        const bool show_icon = state->icon && side_icon && icon_width + 4 <= slot_left && icon_height <= body_height;
        if (state->icon && side_icon && !show_icon) lv_obj_add_flag(state->icon, LV_OBJ_FLAG_HIDDEN);
        const int label_left = show_icon ? icon_width + 4 : 0;
        const int label_width = slot_left - label_left - 4;
        if (state->center_label) {
            lv_label_set_long_mode(state->center_label, LV_LABEL_LONG_DOT);
            const int scale = lv_obj_get_style_transform_scale_x(state->center_label, LV_PART_MAIN);
            lv_obj_set_width(state->center_label, label_width > 0 ? label_width * 256 / (scale > 0 ? scale : 256) : 0);
            lv_obj_set_height(state->center_label, lv_font_get_line_height(lv_obj_get_style_text_font(state->center_label, LV_PART_MAIN)));
            if (label_width <= 0) lv_obj_add_flag(state->center_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_update_layout(state->button);
        }
        const lv_area_t label_bounds = widget_content_bounds(state->center_label);
        const int label_height = state->center_label && label_width > 0
            ? lv_area_get_height(&label_bounds) : 0;
        if (show_icon) lv_obj_set_pos(state->icon, state->offset_x - icon_bounds.x1,
            body_top + (body_height - icon_height) / 2 + state->offset_y - icon_bounds.y1);
        if (state->center_label) {
            if (label_height > body_height) lv_obj_add_flag(state->center_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(state->center_label, label_left + state->offset_x + state->label_offset_x - label_bounds.x1,
                body_top + (body_height - label_height) / 2 + state->offset_y + state->label_offset_y - label_bounds.y1);
        }
        control_x = slot_left + knob / 2;
        control_y = body_top + (body_height - track) / 2;
        lv_obj_set_size(state->control, control_length, track);
    } else {
        const int group_width = content_width - knob - 4;
        lv_obj_t* outer_icon = !side_icon ? state->icon : nullptr;
        if (outer_icon) state->icon = nullptr;
        const SliderContentGroup group = slider_measure_group(state, group_width);
        const bool group_fits = group.height > 0 && group.height <= body_height && group_width > 0;
        if (group.height && !group_fits) slider_hide_content_group(state);
        else if (group_fits)
            slider_place_group(state, knob + 4,
                               body_top + (body_height - group.height) / 2,
                               group_width, group);
        if (outer_icon) state->icon = outer_icon;
        control_x = (knob - track) / 2;
        control_y = body_top + (body_height - control_length) / 2;
        lv_obj_set_size(state->control, track, control_length);
    }
    if (state->icon && !side_icon) lv_obj_set_pos(state->icon,
        (content_width - icon_width) / 2 + state->offset_x - icon_bounds.x1,
        (config.icon_position == 1 ? full_top : full_bottom - icon_height) + state->offset_y - icon_bounds.y1);
    lv_obj_set_style_pad_all(state->control, (knob - track) / 2, LV_PART_KNOB);
    lv_obj_set_ext_click_area(state->control, (24 > knob ? 24 : knob) / 2);
    lv_obj_set_pos(state->control, control_x + state->offset_x, control_y + state->offset_y);
    lv_obj_update_layout(state->button);
    state->layout_usable = usable;
    state->layout_dirty = false;
    slider_render(state);
}

static void slider_mark_layout_dirty(lv_event_t* event) {
    auto* state = static_cast<SliderState*>(lv_event_get_user_data(event));
    if (state) state->layout_dirty = true;
}

static void slider_reconcile(SliderState* state) {
    if (!state->bound) return;
    if (state->pending) slider_set_display(state, state->pending_position);
    else if (state->has_report) slider_set_display(state, state->reported_position);
    else {
        state->available = false;
        slider_set_display(state, 0);
    }
}

static void slider_cancel_gesture(SliderState* state) {
    if (!state->gesture_active) return;
    state->gesture_active = false;
    if (state->bound) slider_reconcile(state);
    else slider_set_display(state, state->gesture_start_position);
}

static void slider_apply_report(SliderState* state, const char* raw_value, bool valid) {
    int64_t value = 0;
    valid = valid && slider_numeric_parse_source(raw_value, state->config->range, &value);
    if (!valid) {
        state->available = false;
        state->has_report = false;
        state->pending = false;
        slider_cancel_gesture(state);
        slider_set_display(state, 0);
        return;
    }
    const uint16_t position = slider_numeric_position_for(state->config->range, value);
    state->available = true;
    state->has_report = true;
    state->reported_position = position;
    if (state->pending && position == state->pending_position) state->pending = false;
    if (state->gesture_active) return;
    if (state->pending) slider_set_display(state, state->pending_position);
    else slider_set_display(state, position);
}

static void slider_delete_guard(lv_event_t* event) {
    *static_cast<bool*>(lv_event_get_user_data(event)) = true;
}

static void slider_release_gesture(SliderState* state) {
    state->draft_position = static_cast<uint16_t>(lv_slider_get_value(state->control));
    if (state->draft_position == state->gesture_start_position) {
        state->gesture_active = false;
        slider_reconcile(state);
        return;
    }

    lv_event_code_t feedback = LV_EVENT_RELEASED;
    if (lv_obj_send_event(state->button, LV_EVENT_VALUE_CHANGED, &feedback) == LV_RESULT_INVALID)
        return;

    if (!state->action_count) {
        state->gesture_active = false;
        if (state->bound) slider_reconcile(state);
        return;
    }

    char value[16];
    if (!slider_numeric_format(slider_numeric_value_at(state->config->range,
                                                       state->draft_position),
                               value, sizeof(value))) {
        slider_cancel_gesture(state);
        return;
    }
    ButtonAction actions[MAX_BUTTON_ACTIONS];
    for (uint8_t index = 0; index < state->action_count; ++index) {
        actions[index] = state->actions[index];
        if (!action_type_substitute_token(action_type_find(actions[index].type), actions[index], "{value}", value)) {
            slider_cancel_gesture(state);
            return;
        }
    }

    const bool was_pending = state->pending;
    const uint16_t previous_pending_position = state->pending_position;
    const uint32_t previous_pending_since = state->pending_since;
    state->gesture_active = false;
    if (state->bound) {
        state->pending = true;
        state->pending_position = state->draft_position;
        state->pending_since = lv_tick_get();
    } else {
        state->reported_position = state->draft_position;
        state->has_report = true;
    }
    slider_set_display(state, state->draft_position);

    lv_obj_t* button = state->button;
    bool deleted = false;
    lv_obj_add_event_cb(button, slider_delete_guard, LV_EVENT_DELETE, &deleted);
    const ActionResult result = action_list_dispatch_result(
        actions, state->action_count, "SliderValue", ACTION_CONTINUATION_OWNER_LVGL);
    if (deleted) return;
    lv_obj_remove_event_cb_with_user_data(button, slider_delete_guard, &deleted);
    if (result == ACTION_FAILED) {
        state->pending = was_pending;
        state->pending_position = previous_pending_position;
        state->pending_since = previous_pending_since;
        if (state->bound) slider_reconcile(state);
        else slider_set_display(state, state->gesture_start_position);
    }
}

static void slider_event(lv_event_t* event) {
    auto* state = static_cast<SliderState*>(lv_event_get_user_data(event));
    if (!state || !state->control) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        if (!state->available || !state->layout_usable) return;
        state->gesture_active = true;
        state->gesture_start_position = state->display_position;
        state->draft_position = state->display_position;
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        if (!state->programmatic_update && state->gesture_active) {
            state->draft_position = static_cast<uint16_t>(lv_slider_get_value(state->control));
            state->display_position = state->draft_position;
            slider_render(state);
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (state->gesture_active) slider_release_gesture(state);
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET) {
        slider_cancel_gesture(state);
    }
}

static void slider_create(lv_obj_t* button, const WidgetConfig* widget,
                          const ScreenButtonConfig* source, const PadRect*,
                          const UIScaleInfo*, lv_obj_t* icon,
                          lv_obj_t* center, WidgetState* runtime) {
    const auto* config = reinterpret_cast<const SliderConfig*>(widget->data);
    auto* state = new (runtime->data) SliderState{};
    state->config = config;
    state->actions = source->actions;
    state->action_count = source->action_count;
    state->button = button;
    state->icon = icon;
    if (icon && lv_obj_check_type(icon, &lv_image_class)) {
        state->icon_scale_x = lv_image_get_scale_x(icon);
        state->icon_scale_y = lv_image_get_scale_y(icon);
    }
    state->center_label = center;
    state->bound = widget->data_binding[0][0] != '\0';
    state->available = !state->bound;
    state->layout_usable = true;
    state->offset_x = source->ui_offset_x;
    state->offset_y = source->ui_offset_y;
    state->label_offset_x = source->style_center.x_offset;
    state->label_offset_y = source->style_center.y_offset;
    for (uint32_t index = 0; index < lv_obj_get_child_count(button); ++index) {
        lv_obj_t* child = lv_obj_get_child(button, index);
        if (child == center || !lv_obj_check_type(child, &lv_label_class)) continue;
        const lv_align_t align = lv_obj_get_style_align(child, LV_PART_MAIN);
        if (align == LV_ALIGN_TOP_MID) state->top_label = child;
        if (align == LV_ALIGN_BOTTOM_MID) state->bottom_label = child;
    }
    if (icon) lv_obj_set_align(icon, LV_ALIGN_TOP_LEFT);
    if (center) lv_obj_set_align(center, LV_ALIGN_TOP_LEFT);

    state->control = lv_slider_create(button);
    if (!state->control) {
        state->available = false;
        state->layout_usable = false;
        return;
    }
    lv_obj_clear_flag(state->control, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(state->control, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(state->control, LV_OBJ_FLAG_PRESS_LOCK);
    lv_slider_set_range(state->control, 0, config->valid_range ? config->range.intervals : 1);
    lv_slider_set_value(state->control, 0, LV_ANIM_OFF);
    lv_obj_add_event_cb(state->control, slider_event, LV_EVENT_ALL, state);
    lv_obj_add_event_cb(button, slider_mark_layout_dirty, LV_EVENT_SIZE_CHANGED, state);
    lv_obj_add_event_cb(button, slider_mark_layout_dirty, LV_EVENT_STYLE_CHANGED, state);
    for (lv_obj_t* child : {state->icon, state->center_label, state->top_label, state->bottom_label}) {
        if (!child) continue;
        lv_obj_add_event_cb(child, slider_mark_layout_dirty, LV_EVENT_SIZE_CHANGED, state);
        lv_obj_add_event_cb(child, slider_mark_layout_dirty, LV_EVENT_STYLE_CHANGED, state);
    }
    const uint32_t track_rgb = widget_default_rgb(DEFAULT_WIDGET_TRACK_COLOR);
    const uint32_t fill_rgb = widget_default_rgb(DEFAULT_WIDGET_SERIES_COLOR);
    const uint32_t knob_rgb = widget_default_rgb(DEFAULT_WIDGET_INDICATOR_COLOR);
    lv_obj_set_style_bg_color(state->control, slider_color(config->track_color, track_rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_color(state->control, slider_color(config->fill_color, fill_rgb), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(state->control, slider_color(config->knob_color, knob_rgb), LV_PART_KNOB);
    for (lv_style_selector_t part : {LV_PART_MAIN, LV_PART_INDICATOR, LV_PART_KNOB}) {
        lv_obj_set_style_bg_opa(state->control, LV_OPA_COVER, part);
        lv_obj_set_style_radius(state->control, LV_RADIUS_CIRCLE, part);
        lv_obj_set_style_border_width(state->control, 0, part);
    }
    lv_obj_set_style_border_width(state->control, 1, LV_PART_KNOB);
    lv_obj_set_style_border_color(state->control, slider_color(config->track_color, track_rgb), LV_PART_KNOB);
    lv_obj_set_style_anim_duration(state->control, 0, LV_PART_MAIN);
    state->layout_dirty = true;
    slider_layout(state);
    slider_set_display(state, 0);
}

static void slider_update(lv_obj_t*, const WidgetConfig*, WidgetState* runtime,
                          const char* raw_value) {
    auto* state = reinterpret_cast<SliderState*>(runtime->data);
    slider_apply_report(state, raw_value, raw_value != nullptr);
}

static void slider_tick(lv_obj_t* button, const WidgetConfig* widget, WidgetState* runtime) {
    auto* state = reinterpret_cast<SliderState*>(runtime->data);
    if (!button || !state->control || !state->config->valid_range) return;
    if (lv_obj_has_flag(button, LV_OBJ_FLAG_HIDDEN) || lv_obj_has_state(button, LV_STATE_DISABLED)) {
        slider_cancel_gesture(state);
        return;
    }
    if (state->bound) {
        char source[BINDING_TEMPLATE_MAX_LEN];
        const bool valid = binding_template_resolve_checked(widget->data_binding[0],
                                                            source, sizeof(source));
        slider_apply_report(state, source, valid);
    }
    if (state->pending && uint32_t(lv_tick_get() - state->pending_since) >= 5000) {
        state->pending = false;
        if (!state->gesture_active) slider_reconcile(state);
    }
    if (state->layout_dirty) slider_layout(state);
}

static void slider_hide(WidgetState* runtime) {
    auto* state = reinterpret_cast<SliderState*>(runtime->data);
    slider_cancel_gesture(state);
    state->pending = false;
    if (state->bound) {
        state->available = false;
        slider_set_display(state, 0);
    }
}

static void slider_show(WidgetState* runtime) {
    auto* state = reinterpret_cast<SliderState*>(runtime->data);
    state->layout_dirty = true;
    if (state->bound) {
        state->available = false;
        slider_set_display(state, 0);
    }
}

static void slider_destroy(WidgetState* runtime) {
    auto* state = reinterpret_cast<SliderState*>(runtime->data);
    slider_cancel_gesture(state);
    state->pending = false;
}

#if HAS_MCP
static void slider_describe(JsonObject& out) {
    JsonArray fields = out.createNestedArray("config_fields");
    auto add_number = [&](const char* name, double minimum, double maximum, double value) {
        JsonObject field = fields.createNestedObject();
        field["name"] = name; field["type"] = "number";
        field["minimum"] = minimum; field["maximum"] = maximum; field["default"] = value;
    };
    add_number("widget_slider_min", -1000000, 1000000, 0);
    add_number("widget_slider_max", -1000000, 1000000, 100);
    add_number("widget_slider_step", 0.0001, 2000000, 1);
    JsonObject axis = fields.createNestedObject();
    axis["name"] = "widget_slider_axis"; axis["type"] = "string"; axis["default"] = "horizontal";
    JsonArray axes = axis.createNestedArray("enum"); axes.add("horizontal"); axes.add("vertical");
    add_number("widget_slider_length_pct", 20, 100, 50);
    add_number("widget_slider_track_px", 0, 32, 0);
    add_number("widget_slider_knob_px", 0, 64, 0);
    for (const char* name : {"widget_slider_track_color", "widget_slider_fill_color", "widget_slider_knob_color"}) {
        JsonObject color = fields.createNestedObject(); color["name"] = name; color["type"] = "color";
    }
    JsonObject placement = fields.createNestedObject();
    placement["name"] = "widget_slider_icon_position"; placement["type"] = "string"; placement["default"] = "left";
    JsonArray positions = placement.createNestedArray("enum");
    for (const char* position : {"left", "above", "below"}) positions.add(position);
    out["note"] = "Touch slider. actions[] runs once on a changed release; {value} is optional in registered value fields. No long press. Length is a percentage of the content width (horizontal) or body height (vertical), including the knob; without a center label or side icon, it uses the full length. Automatic sizing targets a 50px knob and 20px track, shrinking to fit. Bound reports are authoritative, with a five-second confirmation timeout.";
}
#endif

static const WidgetPreview slider_preview = {"Slider", "tune", nullptr, "widget_slider_axis", nullptr, nullptr, "horizontal"};
REGISTER_WIDGET_SCHEMA_INPUT_LIFECYCLE(slider, nullptr, true, slider_validate, slider_cancel_input);

} // namespace

void slider_cancel_input(WidgetState* runtime) {
    if (!runtime) return;
    slider_cancel_gesture(reinterpret_cast<SliderState*>(runtime->data));
}

void slider_attach(WidgetState* runtime, const ButtonAction* actions, uint8_t count,
                   lv_event_cb_t input_guard, void* context) {
    auto* state = reinterpret_cast<SliderState*>(runtime->data);
    state->actions = actions;
    state->action_count = count > MAX_BUTTON_ACTIONS ? MAX_BUTTON_ACTIONS : count;
    if (state->control && input_guard)
        lv_obj_add_event_cb(state->control, input_guard,
            static_cast<lv_event_code_t>(LV_EVENT_ALL | LV_EVENT_PREPROCESS), context);
}

#endif // HAS_DISPLAY && HAS_TOUCH