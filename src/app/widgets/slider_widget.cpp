#include "widget.h"

#if HAS_DISPLAY && HAS_TOUCH

#include "slider_widget.h"
#include "slider_numeric.h"
#include "../action_list.h"
#include "../action_parse.h"
#include "../action_registry.h"
#include "../action_validate.h"
#include "../binding_value.h"
#include <algorithm>
#include <new>
#include <string.h>

namespace {

struct SliderConfig {
    SliderNumericRange range;
    char track_color[8];
    char fill_color[8];
    char knob_color[8];
    char value_color[8];
    char value_format[65];
    uint16_t value_clearance;
    uint16_t value_font_size;
    uint8_t value_side;
    bool show_value;
    uint8_t axis;
    uint8_t length_pct;
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
    lv_obj_t* value_label;
    LabelStyle value_style;
    PadFontCache* value_fonts;
    uint16_t value_width;
    uint16_t display_position;
    uint16_t reported_position;
    uint16_t draft_position;
    uint16_t gesture_start_position;
    uint16_t pending_position;
    uint32_t pending_since;
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
    "widget_slider_knob_color",
    "widget_slider_show_value", "widget_slider_value_font_size", "widget_slider_value_color",
    "widget_slider_value_side",
    "widget_slider_value_format", "widget_slider_value_clearance"
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
    if (slider_has_ordinary_actions(button, "lp_actions")) return "slider does not support long-press actions";
    if (button.containsKey("widget_slider_show_value") && !button["widget_slider_show_value"].is<bool>())
        return "slider show value must be a boolean";
    if (button.containsKey("widget_slider_value_font_size") &&
        (!button["widget_slider_value_font_size"].is<unsigned>() ||
                 button["widget_slider_value_font_size"].as<unsigned>() > 512))
                return "slider value font size must be an integer from 0 to 512";
    if (button.containsKey("widget_slider_value_color") &&
        (!button["widget_slider_value_color"].is<const char*>() ||
         (button["widget_slider_value_color"].as<const char*>()[0] &&
          !slider_color_valid(button, "widget_slider_value_color"))))
        return "slider value color must be empty or a #RRGGBB literal";
    if (button.containsKey("widget_slider_value_side")) {
        if (!button["widget_slider_value_side"].is<const char*>()) return "invalid slider value side";
        const char* side = button["widget_slider_value_side"].as<const char*>();
        if (strcmp(side, "auto") && strcmp(side, "before") && strcmp(side, "after"))
            return "slider value side must be auto, before, or after";
    }
    if (button.containsKey("widget_slider_value_format")) {
        if (!button["widget_slider_value_format"].is<const char*>()) return "slider value format must be a string";
        const char* format = button["widget_slider_value_format"].as<const char*>();
        if (strlen(format) > 64 || (format[0] && !binding_value_format_valid(format, 32, 8, "fFs")))
            return "slider value format needs one %f or %s conversion, at most 64 bytes, width 32, and precision 8";
    }
    if (button.containsKey("widget_slider_value_clearance") &&
        (!button["widget_slider_value_clearance"].is<unsigned>() ||
         button["widget_slider_value_clearance"].as<unsigned>() > 200))
        return "slider value clearance must be an integer from 0 to 200";
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
    config->length_pct = static_cast<uint8_t>(button["widget_slider_length_pct"] | 100);
    config->track_px = static_cast<uint8_t>(button["widget_slider_track_px"] | 0);
    config->knob_px = static_cast<uint8_t>(button["widget_slider_knob_px"] | 0);
    strlcpy(config->track_color, button["widget_slider_track_color"] | DEFAULT_WIDGET_TRACK_COLOR,
            sizeof(config->track_color));
    strlcpy(config->fill_color, button["widget_slider_fill_color"] | DEFAULT_WIDGET_SERIES_COLOR,
            sizeof(config->fill_color));
    strlcpy(config->knob_color, button["widget_slider_knob_color"] | DEFAULT_WIDGET_INDICATOR_COLOR,
            sizeof(config->knob_color));
    config->show_value = button["widget_slider_show_value"] | true;
    config->value_font_size = button["widget_slider_value_font_size"] | 0;
    strlcpy(config->value_color, button["widget_slider_value_color"] | "", sizeof(config->value_color));
    strlcpy(config->value_format, button["widget_slider_value_format"] | "", sizeof(config->value_format));
    config->value_clearance = button["widget_slider_value_clearance"] | 40;
    const char* side = button["widget_slider_value_side"] | "auto";
    config->value_side = !strcmp(side, "before") ? 1 : !strcmp(side, "after") ? 2 : 0;
}

static lv_color_t slider_color(const char* value, uint32_t fallback) {
    uint32_t rgb = fallback;
    parse_hex_color(value, &rgb);
    return lv_color_make((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

static void slider_hide_value(SliderState* state) {
    if (state->value_label) lv_obj_add_flag(state->value_label, LV_OBJ_FLAG_HIDDEN);
}

static void slider_format_value(const SliderConfig* config, int64_t value, char* output, size_t size) {
    char number[16];
    if (!slider_numeric_format(value, number, sizeof(number))) {
        output[0] = '\0';
        return;
    }
    if (config->value_format[0]) binding_value_format(number, config->value_format, output, size);
    else strlcpy(output, number, size);
}

static int slider_value_width(const SliderConfig* config, const lv_font_t* font) {
    char widest_digit = '0';
    int digit_width = 0;
    for (char digit = '0'; digit <= '9'; ++digit) {
        const int width = lv_font_get_glyph_width(font, digit, digit);
        if (width > digit_width) { digit_width = width; widest_digit = digit; }
    }
    int width = 0;
    for (int64_t value : {config->range.minimum, config->range.maximum, int64_t{0}}) {
        char text[96];
        unsigned precision = 4;
        int64_t step = config->range.step;
        int64_t minimum = config->range.minimum;
        while (precision && step % 10 == 0 && minimum % 10 == 0) {
            --precision; step /= 10; minimum /= 10;
        }
        char number[24];
        snprintf(number, sizeof(number), "%.*f", static_cast<int>(precision),
                 static_cast<double>(value) / SLIDER_FIXED_SCALE);
        if (config->value_format[0]) binding_value_format(number, config->value_format, text, sizeof(text));
        else strlcpy(text, number, sizeof(text));
        for (char* cursor = text; *cursor; ++cursor)
            if (*cursor >= '0' && *cursor <= '9') *cursor = widest_digit;
        lv_point_t size;
        lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        width = std::max(width, static_cast<int>(size.x));
    }
    return width + 20;
}

static void slider_update_value(SliderState* state) {
    if (!state->value_label || !state->gesture_active || !state->layout_usable) {
        slider_hide_value(state);
        return;
    }
    char text[96];
    slider_format_value(state->config, slider_numeric_value_at(state->config->range, state->display_position),
                        text, sizeof(text));
    lv_label_set_text(state->value_label, text);
    lv_obj_t* typography = state->button;
    const lv_font_t* font = lv_obj_get_style_text_font(typography, LV_PART_MAIN);
    if (state->config->value_font_size) {
        LabelStyle style = state->value_style;
        style.font_size = state->config->value_font_size;
        font = pad_resolve_font(style, font, state->value_fonts);
    }
    lv_obj_set_style_text_font(state->value_label, font, 0);
    uint32_t rgb;
    const lv_color_t color = parse_hex_color(state->config->value_color, &rgb)
        ? lv_color_hex(rgb) : lv_obj_get_style_text_color(typography, LV_PART_MAIN);
    lv_obj_set_style_text_color(state->value_label, color, 0);
    const lv_color_t background = lv_color_brightness(color) > 128 ? lv_color_hex(0x202124) : lv_color_hex(0xf4f5f6);
    lv_obj_set_style_bg_color(state->value_label, background, 0);
    lv_obj_set_style_bg_opa(state->value_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(state->value_label, 8, 0);
    lv_obj_set_style_pad_hor(state->value_label, 10, 0);
    lv_obj_set_style_pad_ver(state->value_label, 6, 0);
    lv_obj_set_style_text_align(state->value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_shadow_color(state->value_label, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(state->value_label, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(state->value_label, 6, 0);
    lv_display_t* display = lv_obj_get_display(state->button);
    const int screen_width = lv_display_get_horizontal_resolution(display);
    const int screen_height = lv_display_get_vertical_resolution(display);
    if (!state->value_width)
        state->value_width = slider_value_width(state->config, lv_obj_get_style_text_font(state->value_label, LV_PART_MAIN));
    lv_obj_set_width(state->value_label, std::min(screen_width, static_cast<int>(state->value_width)));
    lv_obj_set_style_max_width(state->value_label, screen_width, 0);
    lv_obj_set_style_max_height(state->value_label, screen_height, 0);
    lv_obj_remove_flag(state->value_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(state->value_label);
    lv_obj_update_layout(state->value_label);
    const int width = lv_obj_get_width(state->value_label);
    const int height = lv_obj_get_height(state->value_label);
    lv_area_t track;
    lv_obj_get_content_coords(state->control, &track);
    const int knob_padding = lv_obj_get_style_pad_left(state->control, LV_PART_KNOB);
    const int clearance = knob_padding + state->config->value_clearance;
    int position_x, position_y;
    if (state->config->axis == 0) {
        const int center = (track.y1 + track.y2) / 2;
        position_x = track.x1 + (track.x2 - track.x1) * state->display_position / state->config->range.intervals - width / 2;
        const int above = track.y1 - clearance - height;
        const int below = track.y2 + 1 + clearance;
        position_y = state->config->value_side == 2 ? below : above;
        if (position_y < 0 || position_y + height > screen_height)
            position_y = position_y < center ? below : above;
    } else {
        const int center = (track.x1 + track.x2) / 2;
        position_y = track.y2 - (track.y2 - track.y1) * state->display_position / state->config->range.intervals - height / 2;
        const int left = track.x1 - clearance - width;
        const int right = track.x2 + 1 + clearance;
        position_x = state->config->value_side == 1 ? left : right;
        if (position_x < 0 || position_x + width > screen_width)
            position_x = position_x < center ? right : left;
    }
    lv_obj_set_pos(state->value_label, clamp_val(position_x, 0, std::max(0, screen_width - width)),
                   clamp_val(position_y, 0, std::max(0, screen_height - height)));
}

static void slider_render(SliderState* state) {
    if (!state->control) return;
    const bool usable = state->layout_usable;
    if (usable) lv_obj_clear_state(state->control, LV_STATE_DISABLED);
    else lv_obj_add_state(state->control, LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(state->control, usable && state->available ? LV_OPA_COVER : LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(state->control, usable && state->available && !state->pending ? LV_OPA_COVER : LV_OPA_50,
                            LV_PART_INDICATOR);
    if (usable) lv_obj_clear_flag(state->control, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(state->control, LV_OBJ_FLAG_HIDDEN);
    if (state->value_label && !lv_obj_has_flag(state->value_label, LV_OBJ_FLAG_HIDDEN))
        slider_update_value(state);
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

static void slider_layout(SliderState* state) {
    if (!state->button || !state->control) return;
    const SliderConfig& config = *state->config;
    lv_obj_update_layout(state->button);
    const int content_width = lv_obj_get_content_width(state->button);
    const int body_height = lv_obj_get_content_height(state->button);
    const int target_length = (config.axis == 0 ? content_width : body_height)
        * config.length_pct / 100;
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
    int control_x = 0, control_y = 0;
    if (config.axis == 0) {
        control_x = (content_width - control_length) / 2;
        control_y = (body_height - track) / 2;
        lv_obj_set_size(state->control, control_length, track);
    } else {
        control_x = (content_width - track) / 2;
        control_y = (body_height - control_length) / 2;
        lv_obj_set_size(state->control, track, control_length);
    }
    lv_obj_set_style_pad_all(state->control, (knob - track) / 2, LV_PART_KNOB);
    lv_obj_set_ext_click_area(state->control, (24 > knob ? 24 : knob) / 2);
    lv_obj_set_pos(state->control, control_x, control_y);
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
    slider_hide_value(state);
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
        slider_set_display(state, state->gesture_active ? state->draft_position : 0);
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
    slider_hide_value(state);
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

static void slider_drag_value(SliderState* state) {
    const uint16_t position = static_cast<uint16_t>(lv_slider_get_value(state->control));
    if (position != state->draft_position && state->config->show_value && !state->value_label) {
        state->value_label = lv_label_create(lv_display_get_layer_top(lv_obj_get_display(state->button)));
        if (state->value_label) {
            lv_obj_remove_style_all(state->value_label);
            lv_obj_set_size(state->value_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_clear_flag(state->value_label, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(state->value_label, LV_OBJ_FLAG_SCROLLABLE);
            lv_label_set_long_mode(state->value_label, LV_LABEL_LONG_CLIP);
        }
    }
    if (position != state->draft_position && state->config->show_value && state->value_label)
        lv_obj_remove_flag(state->value_label, LV_OBJ_FLAG_HIDDEN);
    state->draft_position = position;
    state->display_position = position;
    slider_render(state);
}

static void slider_event(lv_event_t* event) {
    auto* state = static_cast<SliderState*>(lv_event_get_user_data(event));
    if (!state || !state->control) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        if (!state->layout_usable) return;
        state->gesture_active = true;
        state->value_width = 0;
        state->gesture_start_position = state->display_position;
        state->draft_position = state->display_position;
        lv_indev_t* input = lv_event_get_indev(event);
        if (input && lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER) {
            lv_point_t point;
            lv_indev_get_point(input, &point);
            lv_obj_transform_point(state->control, &point, LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE_RECURSIVE);
            lv_area_t track;
            lv_obj_get_coords(state->control, &track);
            const bool vertical = state->config->axis != 0;
            const int start_padding = vertical ? lv_obj_get_style_pad_top(state->control, LV_PART_MAIN)
                : lv_obj_get_style_pad_left(state->control, LV_PART_MAIN);
            const int end_padding = vertical ? lv_obj_get_style_pad_bottom(state->control, LV_PART_MAIN)
                : lv_obj_get_style_pad_right(state->control, LV_PART_MAIN);
            const int length = (vertical ? lv_obj_get_height(state->control) : lv_obj_get_width(state->control))
                - start_padding - end_padding;
            const int offset = vertical ? track.y2 - end_padding - point.y : point.x - track.x1 - start_padding;
            if (length > 0) {
                const int position = (clamp_val(offset, 0, length) * state->config->range.intervals + length / 2) / length;
                lv_slider_set_value(state->control, position, LV_ANIM_OFF);
            }
        }
        slider_drag_value(state);
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        if (!state->programmatic_update && state->gesture_active) {
            slider_drag_value(state);
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (state->gesture_active) slider_release_gesture(state);
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET) {
        slider_cancel_gesture(state);
    }
}

static void slider_create(lv_obj_t* button, const WidgetConfig* widget,
                          const ScreenButtonConfig* source, const PadRect*,
                          const UIScaleInfo*, WidgetState* runtime) {
    const auto* config = reinterpret_cast<const SliderConfig*>(widget->data);
    auto* state = new (runtime->data) SliderState{};
    state->config = config;
    state->value_style = source->style_center;
    state->value_fonts = runtime->font_owner;
    state->actions = source->actions;
    state->action_count = source->action_count;
    state->button = button;
    state->bound = widget->data_binding[0][0] != '\0';
    state->available = !state->bound;
    state->layout_usable = true;
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
    if (state->value_label) {
        lv_obj_delete(state->value_label);
        state->value_label = nullptr;
    }
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
    add_number("widget_slider_length_pct", 20, 100, 100);
    add_number("widget_slider_track_px", 0, 32, 0);
    add_number("widget_slider_knob_px", 0, 64, 0);
    for (const char* name : {"widget_slider_track_color", "widget_slider_fill_color", "widget_slider_knob_color"}) {
        JsonObject color = fields.createNestedObject(); color["name"] = name; color["type"] = "color";
    }
    JsonObject show_value = fields.createNestedObject();
    show_value["name"] = "widget_slider_show_value";
    show_value["type"] = "boolean"; show_value["default"] = true;
    add_number("widget_slider_value_font_size", 0, 512, 0);
    JsonObject value_color = fields.createNestedObject();
    value_color["name"] = "widget_slider_value_color"; value_color["type"] = "color"; value_color["default"] = "";
    JsonObject format = fields.createNestedObject();
    format["name"] = "widget_slider_value_format"; format["type"] = "string";
    format["default"] = ""; format["max_length"] = 64;
    format["description"] = "One %f conversion (e.g. %.1f C, %.0f%%), or %s for automatic decimals. Width <=32, precision <=8.";
    add_number("widget_slider_value_clearance", 0, 200, 40);
    JsonObject side = fields.createNestedObject();
    side["name"] = "widget_slider_value_side"; side["type"] = "string"; side["default"] = "auto";
    JsonArray sides = side.createNestedArray("enum");
    for (const char* value : {"auto", "before", "after"}) sides.add(value);
    out["note"] = "Touch slider. actions[] runs once on a changed release; {value} is optional in registered value fields. No long press. Length is a percentage of the widget region width (horizontal) or height (vertical), including the knob. Automatic sizing targets a 50px knob and 20px track, shrinking to fit. Bound reports are authoritative, with a five-second confirmation timeout.";
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