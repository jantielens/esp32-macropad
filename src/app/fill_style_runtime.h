#ifndef FILL_STYLE_RUNTIME_H
#define FILL_STYLE_RUNTIME_H

#include "fill_draw.h"
#include "widgets/widget.h"

struct FillStyleRuntime {
    lv_obj_t* button;
    uint32_t colors[7];
    const ButtonFillStyle* style() const { return reinterpret_cast<const ButtonFillStyle*>(this + 1); }
};

inline bool fill_style_has_scheme(const FillStyleRuntime* runtime, const char* scheme) {
    if (!runtime || !scheme || !scheme[0]) return false;
    for (unsigned index = 0; index < 7; ++index)
        if (strstr(runtime->style()->color(index), scheme)) return true;
    return false;
}

inline void fill_style_release(lv_event_t* event) {
    lv_free(lv_event_get_user_data(event));
}

inline FillStyleRuntime* fill_style_find(lv_obj_t* button) {
    for (uint32_t index = 0; index < lv_obj_get_event_count(button); ++index) {
        lv_event_dsc_t* descriptor = lv_obj_get_event_dsc(button, index);
        if (lv_event_dsc_get_cb(descriptor) == fill_style_release)
            return static_cast<FillStyleRuntime*>(lv_event_dsc_get_user_data(descriptor));
    }
    return nullptr;
}

inline void fill_style_poll(FillStyleRuntime* runtime) {
    if (!runtime) return;
    bool changed = false;
    for (unsigned index = 0; index < 7; ++index) {
        uint32_t color = resolve_color(runtime->style()->color(index), 0);
        if (color != runtime->colors[index]) {
            runtime->colors[index] = color;
            changed = true;
        }
    }
    if (changed) lv_obj_invalidate(runtime->button);
}

template<unsigned Surface>
inline void fill_style_pattern_event(lv_event_t* event) {
    auto* runtime = static_cast<FillStyleRuntime*>(lv_event_get_user_data(event));
    lv_obj_t* object = lv_event_get_current_target_obj(event);
    lv_area_t bounds;
    lv_area_t anchor;
    lv_obj_get_coords(object, &bounds);
    lv_obj_get_coords(runtime->button, &anchor);
    int border = lv_obj_get_style_border_width(object, LV_PART_MAIN);
    bounds.x1 += border;
    bounds.y1 += border;
    bounds.x2 -= border;
    bounds.y2 -= border;
    int radius = lv_obj_get_style_radius(object, LV_PART_MAIN) - border;
    lv_point_t origin = {anchor.x1, anchor.y1};
    const ButtonFillStyle* style = runtime->style();
    uint32_t color = runtime->colors[Surface];
    fill_draw_pattern(lv_event_get_layer(event), bounds, radius, origin, style->patterns[Surface],
                      lv_color_make(color >> 16, color >> 8, color), style->spacing, style->stroke);
}

inline void fill_style_attach_pattern(lv_obj_t* object, FillStyleRuntime* runtime, unsigned surface) {
    if (!object || !runtime || runtime->style()->patterns[surface] == FillPattern::None) return;
    lv_event_cb_t callbacks[] = {fill_style_pattern_event<0>, fill_style_pattern_event<1>,
        fill_style_pattern_event<2>, fill_style_pattern_event<3>, fill_style_pattern_event<4>, fill_style_pattern_event<5>};
    lv_obj_add_event_cb(object, callbacks[surface], LV_EVENT_DRAW_MAIN_END, runtime);
}

inline FillStyleRuntime* fill_style_create(lv_obj_t* button, const ButtonFillStyle* style) {
    if (!style) return nullptr;
    FillStyleRuntime* existing = fill_style_find(button);
    if (existing) return existing;
    auto* runtime = static_cast<FillStyleRuntime*>(lv_malloc(sizeof(FillStyleRuntime) + style->bytes));
    if (!runtime) return nullptr;
    runtime->button = button;
    memcpy(runtime + 1, style, style->bytes);
    for (unsigned index = 0; index < 7; ++index) runtime->colors[index] = resolve_color(style->color(index), 0);
    lv_obj_add_event_cb(button, fill_style_release, LV_EVENT_DELETE, runtime);
    fill_style_attach_pattern(button, runtime, 0);
    return runtime;
}

inline void fill_style_rect_outline_event(lv_event_t* event) {
    auto* runtime = static_cast<FillStyleRuntime*>(lv_event_get_user_data(event));
    lv_obj_t* object = lv_event_get_current_target_obj(event);
    lv_area_t bounds;
    lv_obj_get_coords(object, &bounds);
    int limit = (lv_area_get_width(&bounds) < lv_area_get_height(&bounds) ?
                 lv_area_get_width(&bounds) : lv_area_get_height(&bounds)) / 2;
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_opa = LV_OPA_TRANSP;
    descriptor.border_opa = LV_OPA_COVER;
    descriptor.border_width = fill_geometry_clamp(runtime->style()->outline_width, 0, limit);
    descriptor.radius = lv_obj_get_style_radius(lv_obj_get_parent(object), LV_PART_MAIN);
    uint32_t color = runtime->colors[6];
    descriptor.border_color = lv_color_make(color >> 16, color >> 8, color);
    lv_draw_rect(lv_event_get_layer(event), &descriptor, &bounds);
}

inline void fill_style_attach_rect_outline(lv_obj_t* track, FillStyleRuntime* runtime) {
    if (!runtime || !runtime->style()->outline_width) return;
    lv_obj_t* outline = lv_obj_create(track);
    lv_obj_remove_style_all(outline);
    lv_obj_set_size(outline, lv_obj_get_style_width(track, LV_PART_MAIN), lv_obj_get_style_height(track, LV_PART_MAIN));
    lv_obj_set_pos(outline, 0, 0);
    lv_obj_clear_flag(outline, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_event_cb(outline, fill_style_rect_outline_event, LV_EVENT_DRAW_MAIN_END, runtime);
}

inline void fill_style_arc_outline_event(lv_event_t* event) {
    auto* runtime = static_cast<FillStyleRuntime*>(lv_event_get_user_data(event));
    lv_obj_t* object = lv_event_get_current_target_obj(event);
    lv_area_t bounds;
    lv_obj_get_coords(object, &bounds);
    int radius = (lv_area_get_width(&bounds) < lv_area_get_height(&bounds) ?
                  lv_area_get_width(&bounds) : lv_area_get_height(&bounds)) / 2;
    lv_point_t center = {bounds.x1 + radius, bounds.y1 + radius};
    int start = lv_arc_get_bg_angle_start(object);
    int sweep = lv_arc_get_bg_angle_end(object) - start;
    if (sweep < 0) sweep += 360;
    uint32_t color = runtime->colors[6];
    fill_draw_arc_outline(lv_event_get_layer(event), center, radius,
        lv_obj_get_style_arc_width(object, LV_PART_MAIN), start + lv_arc_get_rotation(object), sweep,
        runtime->style()->outline_width, lv_color_make(color >> 16, color >> 8, color));
}

inline void fill_style_attach_arc_outline(lv_obj_t* arc, FillStyleRuntime* runtime) {
    if (arc && runtime && runtime->style()->outline_width)
        lv_obj_add_event_cb(arc, fill_style_arc_outline_event, LV_EVENT_DRAW_MAIN_END, runtime);
}

#endif