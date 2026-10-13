#ifndef FILL_DRAW_H
#define FILL_DRAW_H

#include "fill_pattern.h"
#include <lvgl.h>
#include <math.h>

inline bool fill_rounded_pixel(const lv_area_t& area, int radius, int horizontal, int vertical) {
    if (horizontal < area.x1 || horizontal > area.x2 || vertical < area.y1 || vertical > area.y2) return false;
    int limit = (lv_area_get_width(&area) < lv_area_get_height(&area) ?
                 lv_area_get_width(&area) : lv_area_get_height(&area)) / 2;
    if (radius > limit) radius = limit;
    if (radius <= 0) return true;
    int delta_x = horizontal < area.x1 + radius ? area.x1 + radius - horizontal :
                  horizontal > area.x2 - radius ? horizontal - area.x2 + radius : 0;
    int delta_y = vertical < area.y1 + radius ? area.y1 + radius - vertical :
                  vertical > area.y2 - radius ? vertical - area.y2 + radius : 0;
    return !delta_x || !delta_y || delta_x * delta_x + delta_y * delta_y <= radius * radius;
}

inline void fill_draw_pattern(lv_layer_t* layer, const lv_area_t& bounds, int radius,
                              const lv_point_t& origin, FillPattern pattern,
                              lv_color_t color, int spacing, int stroke) {
    if (pattern == FillPattern::None) return;
    const lv_area_t& clip = layer->_clip_area;
    lv_area_t visible = {bounds.x1 > clip.x1 ? bounds.x1 : clip.x1,
                         bounds.y1 > clip.y1 ? bounds.y1 : clip.y1,
                         bounds.x2 < clip.x2 ? bounds.x2 : clip.x2,
                         bounds.y2 < clip.y2 ? bounds.y2 : clip.y2};
    if (visible.x1 > visible.x2 || visible.y1 > visible.y2) return;
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = color;
    descriptor.bg_opa = LV_OPA_COVER;
    for (int vertical = visible.y1; vertical <= visible.y2; ++vertical) {
        int run_start = 0;
        bool run_open = false;
        for (int horizontal = visible.x1; horizontal <= visible.x2 + 1; ++horizontal) {
            bool marked = horizontal <= visible.x2 &&
                fill_rounded_pixel(bounds, radius, horizontal, vertical) &&
                fill_pattern_pixel(pattern, horizontal - origin.x, vertical - origin.y, spacing, stroke);
            if (marked && !run_open) {
                run_start = horizontal;
                run_open = true;
            }
            if (!marked && run_open) {
                lv_area_t run = {run_start, vertical, horizontal - 1, vertical};
                lv_draw_rect(layer, &descriptor, &run);
                run_open = false;
            }
        }
    }
}

inline void fill_draw_arc_outline(lv_layer_t* layer, const lv_point_t& center,
                                  int radius, int thickness, int start, int sweep,
                                  int width, lv_color_t color) {
    if (width <= 0 || radius <= 0 || thickness <= 0) return;
    int limit = thickness / 2;
    if (limit < 1) limit = 1;
    if (width > limit) width = limit;
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center = center;
    arc.color = color;
    arc.width = width;
    arc.radius = radius;
    arc.start_angle = start;
    arc.end_angle = start + sweep;
    lv_draw_arc(layer, &arc);
    int inner_radius = radius - thickness + width;
    if (inner_radius > 0 && inner_radius < radius) {
        arc.radius = inner_radius;
        lv_draw_arc(layer, &arc);
    }
    if (sweep >= 360) return;
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = width;
    for (unsigned end = 0; end < 2; ++end) {
        float angle = (start + (end ? sweep : 0)) * 0.017453292519943295f;
        float outer = radius - width * 0.5f;
        float inner = radius - thickness + width * 0.5f;
        line.p1.x = center.x + cosf(angle) * inner;
        line.p1.y = center.y + sinf(angle) * inner;
        line.p2.x = center.x + cosf(angle) * outer;
        line.p2.y = center.y + sinf(angle) * outer;
        lv_draw_line(layer, &line);
    }
}

#endif