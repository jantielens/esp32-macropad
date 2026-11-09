#ifndef BUTTON_CONTENT_LVGL_H
#define BUTTON_CONTENT_LVGL_H

#include "pad_layout.h"
#include "widgets/numericrocker_zones.h"

struct ButtonContentRuntime {
    ButtonContentLayoutInput input;
    ButtonContentLayout layout;
    lv_obj_t* labels[3];
    lv_obj_t* label_regions[3];
    lv_obj_t* icon;
    lv_obj_t* icon_region;
    lv_obj_t* widget_region;
    int16_t offset_x;
    int16_t offset_y;
    int16_t label_offset_x[3];
    int16_t label_offset_y[3];
    uint16_t label_scale[3];
    bool label_wrap[3];
    uint16_t icon_scale;
    uint8_t icon_badge_padding;
    bool icon_is_mono;
    bool icon_badge_auto_color;
    bool numeric_center;
    float small_step;
    float large_step;
    uint8_t zone_scale_pct;
    int32_t indicator_inset;
};

inline lv_obj_t* button_content_region(lv_obj_t* parent, const ButtonContentRect& rect,
                                       int16_t offset_x, int16_t offset_y) {
    lv_obj_t* region = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(region, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(region, 0, 0);
    lv_obj_set_style_pad_all(region, 0, 0);
    lv_obj_set_style_radius(region, 0, 0);
    lv_obj_clear_flag(region, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_size(region, rect.width, rect.height);
    lv_obj_set_pos(region, rect.x + offset_x, rect.y + offset_y);
    if (rect.width <= 0 || rect.height <= 0) lv_obj_add_flag(region, LV_OBJ_FLAG_HIDDEN);
    return region;
}

inline void pad_draw_icon_badge(lv_event_t* event) {
    auto* icon = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
    if (lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN)) return;
    auto* region = static_cast<lv_obj_t*>(lv_event_get_target(event));
    lv_area_t area;
    lv_obj_get_coords(region, &area);
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = lv_obj_get_style_bg_color(region, LV_PART_MAIN);
    descriptor.bg_opa = LV_OPA_COVER;
    descriptor.radius = LV_RADIUS_CIRCLE;
    lv_draw_rect(lv_event_get_layer(event), &descriptor, &area);
}

inline lv_color_t pad_auto_icon_badge_color(lv_obj_t* tile, const ButtonContentRuntime& runtime) {
    const lv_color_t foreground = runtime.icon_is_mono && runtime.icon
        ? lv_obj_get_style_image_recolor(runtime.icon, LV_PART_MAIN)
        : lv_obj_get_style_text_color(tile, LV_PART_MAIN);
    return lv_color_mix(foreground,
                        lv_obj_get_style_bg_color(tile, LV_PART_MAIN), 38);
}

inline void pad_update_auto_icon_badge_color(lv_obj_t* tile, ButtonContentRuntime& runtime) {
    if (runtime.icon_badge_auto_color && runtime.icon_region)
        lv_obj_set_style_bg_color(runtime.icon_region, pad_auto_icon_badge_color(tile, runtime), 0);
}

inline void pad_icon_badge_draw_bounds(lv_event_t* event) {
    auto* icon = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto* region = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
    lv_area_t image_area, badge_area;
    lv_obj_get_coords(icon, &image_area);
    lv_obj_get_coords(region, &badge_area);
    int32_t* size = static_cast<int32_t*>(lv_event_get_param(event));
    const int32_t margins[] = {image_area.x1 - badge_area.x1, image_area.y1 - badge_area.y1,
        badge_area.x2 - image_area.x2, badge_area.y2 - image_area.y2};
    for (int32_t margin : margins) if (margin > *size) *size = margin;
}

inline void pad_measure_content_label(ButtonContentRuntime& runtime, int index, const char* text) {
    lv_obj_t* label = runtime.labels[index];
    if (!label) return;
    const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    lv_point_t size{};
    lv_text_get_size(&size, text, font,
                     lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
                     lv_obj_get_style_text_line_space(label, LV_PART_MAIN),
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    runtime.input.label_width[index] = size.x * runtime.label_scale[index] / LV_SCALE_NONE;
    const int32_t measured_height = size.y * runtime.label_scale[index] / LV_SCALE_NONE;
    const int32_t line_height = lv_font_get_line_height(font) * runtime.label_scale[index] / LV_SCALE_NONE;
    runtime.input.label_height[index] = measured_height > line_height ? measured_height : line_height;
}

inline void pad_set_content_label_text(ButtonContentRuntime& runtime, int index, const char* text) {
    // DOT mode mutates LVGL's text buffer; retain natural metrics from the source.
    pad_measure_content_label(runtime, index, text);
    lv_label_set_text(runtime.labels[index], text);
}

inline ButtonContentLayout pad_measure_content_layout(lv_obj_t* tile, ButtonContentRuntime& runtime) {
    lv_obj_update_layout(tile);
    runtime.input.bounds = {0, 0, lv_obj_get_content_width(tile), lv_obj_get_content_height(tile)};
    runtime.input.icon_label_gap = lv_obj_get_style_pad_left(tile, LV_PART_MAIN);
    if (runtime.input.centered_group) {
        const int32_t span = runtime.input.center_horizontal
            ? runtime.input.bounds.width : runtime.input.bounds.height;
        if (runtime.numeric_center) {
            const NRZoneLayout zones = nr_compute_zones(span, runtime.small_step,
                                                        runtime.large_step, runtime.zone_scale_pct);
            runtime.input.center_start = zones.inner_end;
            runtime.input.center_end = zones.inner2_start;
        } else {
            runtime.input.center_start = runtime.indicator_inset;
            runtime.input.center_end = span - runtime.indicator_inset;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (runtime.labels[i] && runtime.label_wrap[i])
            pad_measure_content_label(runtime, i, lv_label_get_text(runtime.labels[i]));
    }
    ButtonContentLayout layout = button_content_layout(runtime.input);
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* label = runtime.labels[i];
        if (!label || !runtime.label_wrap[i]) continue;
        const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
        const int32_t width = layout.labels[i].width * LV_SCALE_NONE / runtime.label_scale[i];
        lv_point_t size{};
        lv_text_get_size(&size, lv_label_get_text(label), font,
                         lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
                         lv_obj_get_style_text_line_space(label, LV_PART_MAIN),
                         width > 0 ? width : 1, LV_TEXT_FLAG_NONE);
        const int32_t measured_height = size.y * runtime.label_scale[i] / LV_SCALE_NONE;
        const int32_t line_height = lv_font_get_line_height(font) * runtime.label_scale[i] / LV_SCALE_NONE;
        runtime.input.label_height[i] = measured_height > line_height ? measured_height : line_height;
    }
    return button_content_layout(runtime.input);
}

inline void pad_position_content_region(lv_obj_t* region, const ButtonContentRect& rect,
                                        int16_t offset_x, int16_t offset_y) {
    lv_obj_set_size(region, rect.width, rect.height);
    lv_obj_set_pos(region, rect.x + offset_x, rect.y + offset_y);
    if (rect.width <= 0 || rect.height <= 0) lv_obj_add_flag(region, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(region, LV_OBJ_FLAG_HIDDEN);
}

inline void pad_reflow_content_layout(ButtonContentRuntime& runtime, const ButtonContentLayout& layout) {
    for (int i = 0; i < 3; ++i) {
        if (!runtime.labels[i]) continue;
        const ButtonContentRect& rect = layout.labels[i];
        pad_position_content_region(runtime.label_regions[i], rect,
                                    runtime.offset_x + runtime.label_offset_x[i],
                                    runtime.offset_y + runtime.label_offset_y[i]);
        const bool standalone_center = i == 1 && !runtime.input.has_widget &&
            !runtime.input.has_icon && !runtime.input.has_label[0] && !runtime.input.has_label[2];
        const int32_t height = standalone_center ? runtime.input.label_height[i] : rect.height;
        const int32_t label_height = height * LV_SCALE_NONE / runtime.label_scale[i];
        lv_obj_set_size(runtime.labels[i], rect.width * LV_SCALE_NONE / runtime.label_scale[i],
                        label_height);
        if (standalone_center) {
            // Center the full text line, even when its region must clip it.
            lv_obj_set_y(runtime.labels[i],
                         (rect.height - label_height * runtime.label_scale[i] / LV_SCALE_NONE) / 2);
        }
    }
    if (runtime.icon) {
        const ButtonContentRect& rect = layout.icon;
        pad_position_content_region(runtime.icon_region, rect, runtime.offset_x, runtime.offset_y);
        if (runtime.input.icon_width > 0) {
            uint32_t fitted_scale;
            if (runtime.icon_badge_padding) {
                const int32_t available = button_layout_nonnegative(rect.width - 2 * runtime.icon_badge_padding);
                const int32_t natural = runtime.input.icon_width - 2 * runtime.icon_badge_padding;
                fitted_scale = natural > 0 ? (uint32_t)runtime.icon_scale * available / natural : 0;
            } else {
                fitted_scale = (uint32_t)runtime.icon_scale * rect.width / runtime.input.icon_width;
            }
            lv_image_set_scale(runtime.icon, fitted_scale > 0 ? fitted_scale : 1);
            if (runtime.icon_badge_padding) {
                const int32_t width = lv_obj_get_width(runtime.icon) * lv_image_get_scale_x(runtime.icon) / LV_SCALE_NONE;
                const int32_t height = lv_obj_get_height(runtime.icon) * lv_image_get_scale_y(runtime.icon) / LV_SCALE_NONE;
                lv_obj_set_pos(runtime.icon, (rect.width - width) / 2, (rect.height - height) / 2);
                // Hiding the icon must also invalidate the circle outside its image bounds.
                lv_obj_refresh_ext_draw_size(runtime.icon);
            }
        }
    }
    if (runtime.widget_region)
        pad_position_content_region(runtime.widget_region, layout.widget, runtime.offset_x, runtime.offset_y);
    runtime.layout = layout;
}

inline ButtonContentLayout pad_apply_content_layout(lv_obj_t* tile, const ScreenButtonConfig& button,
                                                    lv_obj_t* icon, lv_obj_t* labels[3],
                                                    const UIScaleInfo& scale, PadFontCache* fonts,
                                                    ButtonContentRuntime* saved = nullptr,
                                                    bool icon_is_mono = false) {
    lv_obj_update_layout(tile);
    const LabelStyle* styles[] = {&button.style_top, &button.style_center, &button.style_bottom};
    const char* texts[] = {button.label_top, button.label_center, button.label_bottom};
    const lv_font_t* fallback[] = {scale.font_small, scale.font_large, scale.font_small};
    ButtonContentRuntime local{};
    ButtonContentRuntime& runtime = saved ? *saved : local;
    runtime = {};
    ButtonContentLayoutInput& input = runtime.input;
    input.bounds = {0, 0, lv_obj_get_content_width(tile), lv_obj_get_content_height(tile)};
    input.widget_position = button.widget_position;
    input.icon_position = button.icon_position;
    input.group_size_pct = button.group_size_pct;
    input.has_widget = button.widget.type[0];
    const bool rocker = strcmp(button.widget.type, "rocker") == 0;
    runtime.numeric_center = strcmp(button.widget.type, "numericrocker") == 0;
    input.centered_group = (rocker || runtime.numeric_center) && !button.rocker_group_beside;
    if (input.centered_group) {
        if (runtime.numeric_center) {
            const auto* cfg = reinterpret_cast<const NumericRockerConfig*>(button.widget.data);
            input.center_horizontal = cfg->horizontal;
            runtime.small_step = cfg->small_step;
            runtime.large_step = cfg->large_step;
            runtime.zone_scale_pct = cfg->zone_scale_pct;
        } else {
            input.center_horizontal = button.widget.data[0];
            lv_point_t symbol_a{}, symbol_b{};
            lv_text_get_size(&symbol_a, input.center_horizontal ? LV_SYMBOL_LEFT : LV_SYMBOL_UP,
                             scale.font_small, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            lv_text_get_size(&symbol_b, input.center_horizontal ? LV_SYMBOL_RIGHT : LV_SYMBOL_DOWN,
                             scale.font_small, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            const int32_t a = input.center_horizontal ? symbol_a.x : symbol_a.y;
            const int32_t b = input.center_horizontal ? symbol_b.x : symbol_b.y;
            runtime.indicator_inset = (a > b ? a : b) + (input.center_horizontal ? 2 : 1) + 4;
        }
    }
    input.has_icon = icon != nullptr;
    runtime.icon = icon;
    runtime.icon_is_mono = icon_is_mono;
    runtime.offset_x = button.ui_offset_x;
    runtime.offset_y = button.ui_offset_y;
    if (icon) {
        lv_obj_update_layout(icon);
        runtime.icon_badge_padding = button.icon_badge_padding;
        runtime.icon_badge_auto_color = strcmp(button.icon_badge_color, "auto") == 0 ||
                                        !button.icon_badge_color[0];
        runtime.icon_scale = lv_image_get_scale_x(icon);
        input.icon_width = lv_obj_get_width(icon) * lv_image_get_scale_x(icon) / LV_SCALE_NONE;
        input.icon_height = lv_obj_get_height(icon) * lv_image_get_scale_y(icon) / LV_SCALE_NONE;
        if (runtime.icon_badge_padding) {
            const int32_t diameter = button_layout_icon_badge_size(input.icon_width, input.icon_height,
                                                                   runtime.icon_badge_padding);
            input.icon_width = input.icon_height = diameter;
        }
    }
    for (int i = 0; i < 3; ++i) {
        input.has_label[i] = labels[i] != nullptr;
        const lv_font_t* font = pad_resolve_font(*styles[i], fallback[i], fonts);
        runtime.labels[i] = labels[i];
        runtime.label_offset_x[i] = styles[i]->x_offset;
        runtime.label_offset_y[i] = styles[i]->y_offset;
        runtime.label_scale[i] = styles[i]->font_upscale && pad_is_bitmap_font(font)
            ? styles[i]->font_upscale : LV_SCALE_NONE;
        runtime.label_wrap[i] = styles[i]->long_mode == LABEL_MODE_WRAP;
        pad_measure_content_label(runtime, i, texts[i]);
    }
    ButtonContentLayout layout = pad_measure_content_layout(tile, runtime);
    for (int i = 0; i < 3; ++i) {
        if (!labels[i]) continue;
        const ButtonContentRect& rect = layout.labels[i];
        lv_obj_t* region = button_content_region(tile, rect,
            button.ui_offset_x + styles[i]->x_offset, button.ui_offset_y + styles[i]->y_offset);
        runtime.label_regions[i] = region;
        lv_obj_set_parent(labels[i], region);
        lv_obj_align(labels[i], LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_transform_pivot_x(labels[i], 0, 0);
        lv_obj_set_style_transform_pivot_y(labels[i], 0, 0);
        lv_obj_set_style_text_align(labels[i], pad_resolve_align(*styles[i]), 0);
    }
    if (icon) {
        const ButtonContentRect& rect = layout.icon;
        lv_obj_t* region = button_content_region(tile, rect, button.ui_offset_x, button.ui_offset_y);
        if (runtime.icon_badge_padding) {
            uint32_t color;
            const lv_color_t initial = parse_hex_color(button.icon_badge_color, &color)
                ? lv_color_hex(color) : pad_auto_icon_badge_color(tile, runtime);
            lv_obj_set_style_bg_color(region, initial, 0);
            lv_obj_add_event_cb(region, pad_draw_icon_badge, LV_EVENT_DRAW_MAIN, icon);
        }
        runtime.icon_region = region;
        lv_obj_set_parent(icon, region);
        lv_image_set_pivot(icon, 0, 0);
        lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);
        if (runtime.icon_badge_padding)
            lv_obj_add_event_cb(icon, pad_icon_badge_draw_bounds, LV_EVENT_REFR_EXT_DRAW_SIZE, region);
    }
    pad_reflow_content_layout(runtime, layout);
    return layout;
}

#endif
