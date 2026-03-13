#include "widget.h"

#if HAS_DISPLAY

#include "../binding_template.h"
#include "../log_manager.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TAG "Gauge"

// ============================================================================
// Gauge Widget
// ============================================================================
// Renders a semi-circular (or configurable arc) gauge inside the button tile.
// The arc fill angle, color, and needle position are driven by an MQTT/binding
// value (via the existing widget binding system).
//
// Layout within the tile (180° example):
//   ┌──────────────────────┐
//   │   label_top (title)   │
//   │     ╭─▓▓▓▓▓▓▓▓▓─╮    │
//   │   ╭─▓▓▓▓▓▓▓▓▓▓▓▓▓─╮  │
//   │   │▓▓    🔌    ▓▓│  │   ← icon + center label inside arc
//   │   │▓  label_ctr   ▓│  │
//   │   ╰─────────────────╯  │
//   │  label_bottom (units)  │
//   └──────────────────────┘
//
// The icon and center label are repositioned to the visual center of the arc.
//
// LVGL lv_line note: all point coordinates must be non-negative because LVGL
// computes the widget bounding box from max(x), max(y) of the points — negative
// values produce a zero-size bbox and the line becomes invisible.  We compute
// absolute tile-space endpoints, then translate to a non-negative local frame
// and position the lv_obj at the bbox top-left.

// ---- Config struct (packed into WidgetConfig.data[]) ----

struct GaugeConfig {
    float    min_value;              // Scale minimum (default 0)
    float    max_value;              // Scale maximum (default 100)
    uint16_t arc_degrees;            // Arc span (10–360, default 180)
    uint16_t start_angle;            // Rotation offset in degrees (default 180)
    char     arc_color[CONFIG_COLOR_MAX_LEN];               // Arc indicator color (bindable, default "#4CAF50")
    char     arc_color_2[CONFIG_COLOR_MAX_LEN];             // Ring 2 indicator color (bindable, default "#2196F3")
    char     arc_color_3[CONFIG_COLOR_MAX_LEN];             // Ring 3 indicator color (bindable, default "#9C27B0")
    char     track_color[CONFIG_BINDABLE_SHORT_LEN];        // Inactive arc background (default "#1A1A1A")
    char     needle_color[CONFIG_BINDABLE_SHORT_LEN];       // Needle color (default "#FFFFFF")
    char     tick_color[CONFIG_BINDABLE_SHORT_LEN];          // Tick mark color (default "#808080")
    uint8_t  arc_width_pct;          // Arc thickness as % of radius (5–50, default 15)
    uint8_t  tick_count;             // Major tick count (0 = none, default 5)
    uint8_t  needle_width;           // Needle line width in pixels (1–10, default 2)
    uint8_t  tick_width;             // Tick line width in pixels (1–5, default 1)
    bool     show_needle;            // Draw a needle line (default true)
    bool     zero_centered;          // Arc fills from zero point instead of min (default false)
};

static_assert(sizeof(GaugeConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "GaugeConfig exceeds WIDGET_CONFIG_MAX_BYTES");

// ---- Runtime state (packed into WidgetState.data[]) ----

struct GaugeState {
    lv_obj_t* arc_bg;              // Outer ring arc (track + indicator)
    lv_obj_t* arc_ring2;           // Middle ring arc (or nullptr)
    lv_obj_t* arc_ring3;           // Inner ring arc (or nullptr)
    lv_obj_t* needle;              // Needle line object (or nullptr)
    lv_point_precise_t* n_pts;     // Heap-allocated needle points (2 elements)
    lv_obj_t** tick_lines;         // Heap-allocated tick line pointers (or nullptr)
    float     last_value;          // Last numeric value for outer ring
    float     last_value_2;        // Last numeric value for middle ring
    float     last_value_3;        // Last numeric value for inner ring
    int16_t   cx, cy;              // Arc geometric center in tile coords
    int16_t   needle_len;          // Needle length in pixels
    uint8_t   ring_count;          // Number of active rings (1–3)
    uint8_t   tick_line_count;     // Number of cached tick line pointers
    // Color cache (skip LVGL setters when unchanged)
    uint32_t  cached_track;        // track_color
    uint32_t  cached_needle;       // needle_color
    uint32_t  cached_tick;         // tick_color
    uint32_t  cached_arc1;         // arc_color (indicator)
    uint32_t  cached_arc2;         // arc_color_2 (indicator)
    uint32_t  cached_arc3;         // arc_color_3 (indicator)
};

static_assert(sizeof(GaugeState) <= WIDGET_STATE_MAX_BYTES,
              "GaugeState exceeds WIDGET_STATE_MAX_BYTES");

// ---- Helper: compute arc bounding box (unit circle) ----
// Returns the min/max x and y extents of the arc on a unit circle.

static void gauge_arc_bbox(uint16_t start_deg, uint16_t arc_deg,
                           float* out_x_min, float* out_x_max,
                           float* out_y_min, float* out_y_max) {
    float xmn = 0.0f, xmx = 0.0f, ymn = 0.0f, ymx = 0.0f;
    for (uint16_t i = 0; i <= arc_deg; i++) {
        float a = (float)(start_deg + i) * (float)M_PI / 180.0f;
        float x = cosf(a), y = sinf(a);
        if (x < xmn) xmn = x;
        if (x > xmx) xmx = x;
        if (y < ymn) ymn = y;
        if (y > ymx) ymx = y;
    }
    *out_x_min = xmn;  *out_x_max = xmx;
    *out_y_min = ymn;  *out_y_max = ymx;
}

// ---- Helper: create an lv_line between two absolute tile-space points ----
// Translates to non-negative local coordinates and positions accordingly.

static lv_obj_t* gauge_create_line(lv_obj_t* parent,
                                   int16_t x1, int16_t y1,
                                   int16_t x2, int16_t y2,
                                   lv_color_t color, int16_t width) {
    int16_t min_x = (x1 < x2) ? x1 : x2;
    int16_t min_y = (y1 < y2) ? y1 : y2;

    // Heap-allocate points (freed on LV_EVENT_DELETE)
    lv_point_precise_t* pts =
        (lv_point_precise_t*)lv_malloc(sizeof(lv_point_precise_t) * 2);
    if (!pts) return nullptr;
    pts[0].x = x1 - min_x;  pts[0].y = y1 - min_y;
    pts[1].x = x2 - min_x;  pts[1].y = y2 - min_y;

    lv_obj_t* line = lv_line_create(parent);
    lv_line_set_points(line, pts, 2);
    lv_obj_set_pos(line, min_x, min_y);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_clear_flag(line, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

    // Free the points array when the line object is deleted
    lv_obj_add_event_cb(line, [](lv_event_t* e) {
        lv_free(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, pts);

    return line;
}

// ---- Helper: position needle line with non-negative coords ----

static void gauge_set_needle(lv_obj_t* needle, lv_point_precise_t* pts,
                             int16_t cx, int16_t cy,
                             int16_t needle_len, float angle_rad) {
    int16_t tip_x = cx + (int16_t)roundf(cosf(angle_rad) * needle_len);
    int16_t tip_y = cy + (int16_t)roundf(sinf(angle_rad) * needle_len);

    int16_t min_x = (cx < tip_x) ? cx : tip_x;
    int16_t min_y = (cy < tip_y) ? cy : tip_y;

    pts[0].x = cx - min_x;    pts[0].y = cy - min_y;
    pts[1].x = tip_x - min_x; pts[1].y = tip_y - min_y;

    lv_line_set_points(needle, pts, 2);
    lv_obj_set_pos(needle, min_x, min_y);
}

// ---- Helper: create a styled arc ring ----
static lv_obj_t* gauge_create_arc(lv_obj_t* tile, const GaugeConfig* cfg,
                                   int16_t cx, int16_t cy, int16_t radius,
                                   int16_t arc_width) {
    lv_obj_t* arc = lv_arc_create(tile);
    lv_obj_set_size(arc, radius * 2, radius * 2);
    lv_obj_set_pos(arc, cx - radius, cy - radius);
    lv_arc_set_rotation(arc, cfg->start_angle);
    lv_arc_set_bg_angles(arc, 0, cfg->arc_degrees);
    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
    // Track
    lv_obj_set_style_arc_color(arc, resolve_lv_color(cfg->track_color, 0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, arc_width, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    // Indicator
    lv_obj_set_style_arc_color(arc, lv_color_make(0x4C, 0xAF, 0x50), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, arc_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    // Padding / knob
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    // Non-interactive
    lv_obj_clear_flag(arc, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    return arc;
}

static void gauge_create_ticks_for_radius(lv_obj_t* tile, GaugeState* st,
                                          const GaugeConfig* cfg,
                                          int16_t cx, int16_t cy,
                                          int16_t radius, int16_t arc_width,
                                          lv_color_t tick_color) {
    int16_t tick_outer = radius - 1;
    int16_t tick_inner = radius - arc_width + 1;
    if (tick_inner < 1) tick_inner = 1;

    for (uint8_t i = 1; i <= cfg->tick_count; i++) {
        float angle = (float)cfg->start_angle + (float)cfg->arc_degrees * i / (cfg->tick_count + 1);
        float a_rad = angle * (float)M_PI / 180.0f;
        float cos_a = cosf(a_rad);
        float sin_a = sinf(a_rad);

        int16_t x1 = cx + (int16_t)roundf(cos_a * tick_inner);
        int16_t y1 = cy + (int16_t)roundf(sin_a * tick_inner);
        int16_t x2 = cx + (int16_t)roundf(cos_a * tick_outer);
        int16_t y2 = cy + (int16_t)roundf(sin_a * tick_outer);

        lv_obj_t* tl = gauge_create_line(tile, x1, y1, x2, y2,
                          tick_color, cfg->tick_width);
        if (tl && st->tick_lines)
            st->tick_lines[st->tick_line_count++] = tl;
    }
}

// ---- WidgetType callbacks ----

static void gauge_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<GaugeConfig*>(data);
    memset(cfg, 0, sizeof(GaugeConfig));

    cfg->min_value  = btn["widget_gauge_min"] | 0.0f;
    cfg->max_value  = btn["widget_gauge_max"] | 100.0f;

    int deg = btn["widget_gauge_degrees"] | 180;
    cfg->arc_degrees = (uint16_t)clamp_val(deg, 10, 360);

    int sa = btn["widget_gauge_start_angle"] | 180;
    cfg->start_angle = (uint16_t)(sa % 360);

    cfg->show_needle     = btn["widget_gauge_show_needle"] | true;
    cfg->zero_centered   = btn["widget_gauge_zero_centered"] | false;

    // Per-ring arc indicator colors (bindable — may contain threshold() expressions)
    widget_parse_field(btn["widget_arc_color"],   cfg->arc_color,   sizeof(cfg->arc_color),   "#4CAF50");
    widget_parse_field(btn["widget_arc_color_2"], cfg->arc_color_2, sizeof(cfg->arc_color_2), "#2196F3");
    widget_parse_field(btn["widget_arc_color_3"], cfg->arc_color_3, sizeof(cfg->arc_color_3), "#9C27B0");

    widget_parse_field(btn["widget_gauge_track_color"],  cfg->track_color,  sizeof(cfg->track_color),  "#1A1A1A");
    widget_parse_field(btn["widget_gauge_needle_color"], cfg->needle_color, sizeof(cfg->needle_color), "#FFFFFF");
    widget_parse_field(btn["widget_gauge_tick_color"],   cfg->tick_color,   sizeof(cfg->tick_color),   "#808080");

    uint8_t awpct = btn["widget_gauge_arc_width_pct"] | (uint8_t)15;
    cfg->arc_width_pct = clamp_val<uint8_t>(awpct, 5, 50);

    uint8_t tc = btn["widget_gauge_ticks"] | (uint8_t)5;
    cfg->tick_count = (tc > 20) ? 20 : tc;

    uint8_t nw = btn["widget_gauge_needle_width"] | (uint8_t)2;
    cfg->needle_width = (nw > 10) ? 10 : nw;  // 0 = no needle

    uint8_t tw = btn["widget_gauge_tick_width"] | (uint8_t)1;
    cfg->tick_width = clamp_val<uint8_t>(tw, 1, 5);
}

static void gauge_create(lv_obj_t* tile, const WidgetConfig* wcfg,
                          const ScreenButtonConfig* btn,
                          const PadRect* rect, const UIScaleInfo* scale,
                          lv_obj_t* icon_img, WidgetState* state) {
    auto* cfg = reinterpret_cast<const GaugeConfig*>(wcfg->data);
    auto* st = reinterpret_cast<GaugeState*>(state->data);
    memset(st, 0, sizeof(GaugeState));
    st->last_value = NAN;
    st->last_value_2 = NAN;
    st->last_value_3 = NAN;
    st->tick_line_count = 0;
    st->cached_track  = COLOR_CACHE_INIT;
    st->cached_needle = COLOR_CACHE_INIT;
    st->cached_tick   = COLOR_CACHE_INIT;
    st->cached_arc1   = COLOR_CACHE_INIT;
    st->cached_arc2   = COLOR_CACHE_INIT;
    st->cached_arc3   = COLOR_CACHE_INIT;

    // Determine ring count from presence of extra data bindings.
    // If binding[2] is set but binding[1] is empty, treat binding[2] as binding[1]
    // (promote to middle ring to avoid a visual gap).
    uint8_t ring_count = 1;
    if (wcfg->data_binding[1][0] && wcfg->data_binding[2][0]) ring_count = 3;
    else if (wcfg->data_binding[1][0] || wcfg->data_binding[2][0]) ring_count = 2;
    st->ring_count = ring_count;

    // ---- Compute available space ----
    bool has_top = btn->label_top[0];
    bool has_bot = btn->label_bottom[0];
    int16_t label_h = lv_font_get_line_height(scale->font_small) + 2;
    int16_t top_h = has_top ? label_h : 0;
    int16_t bot_h = has_bot ? label_h : 0;

    lv_obj_update_layout(tile);
    int16_t content_h = (int16_t)lv_obj_get_content_height(tile);
    int16_t content_w = (int16_t)lv_obj_get_content_width(tile);

    int16_t avail_h = content_h - top_h - bot_h - 4;
    int16_t avail_w = content_w - 4;
    if (avail_h < 20) avail_h = 20;
    if (avail_w < 20) avail_w = 20;

    // ---- Compute arc bounding box on unit circle ----
    float x_min, x_max, y_min, y_max;
    gauge_arc_bbox(cfg->start_angle, cfg->arc_degrees,
                   &x_min, &x_max, &y_min, &y_max);
    float x_span = x_max - x_min;
    float y_span = y_max - y_min;
    if (x_span < 0.01f) x_span = 1.0f;
    if (y_span < 0.01f) y_span = 1.0f;

    // ---- Fit radius so arc bbox fits available space ----
    int16_t r_from_w = (int16_t)(avail_w / x_span);
    int16_t r_from_h = (int16_t)(avail_h / y_span);
    int16_t radius = (r_from_w < r_from_h) ? r_from_w : r_from_h;
    if (radius < 15) radius = 15;

    int16_t arc_width = (int16_t)(radius * cfg->arc_width_pct / 100);
    if (arc_width < 3) arc_width = 3;

    // ---- Compute geometric center of arc circle in tile coords ----
    // Place center so the arc bbox is centered in available space.
    int16_t arc_pixel_h = (int16_t)(y_span * radius);
    int16_t cx = content_w / 2 - (int16_t)((x_min + x_max) * radius / 2);
    int16_t cy = top_h + (avail_h - arc_pixel_h) / 2 - (int16_t)(y_min * radius);

    st->cx = cx;
    st->cy = cy;
    // Needle extends from center outward past the arc outer edge
    st->needle_len = radius + 4;
    if (st->needle_len < 10) st->needle_len = 10;

    LOGD(TAG, "Layout: rect=%dx%d avail=%dx%d radius=%d arc_w=%d cx=%d cy=%d rings=%d",
         rect->w, rect->h, avail_w, avail_h, radius, arc_width, cx, cy, ring_count);

    // ---- Create outer arc (primary ring) ----
    st->arc_bg = gauge_create_arc(tile, cfg, cx, cy, radius, arc_width);

    // ---- Gap between concentric rings ----
    int16_t ring_gap = (arc_width >= 8) ? (arc_width / 4) : 2;

    // ---- Create middle ring (ring 2) ----
    if (ring_count >= 2) {
        int16_t r2 = radius - arc_width - ring_gap;
        if (r2 < 8) r2 = 8;
        st->arc_ring2 = gauge_create_arc(tile, cfg, cx, cy, r2, arc_width);
    }

    // ---- Create inner ring (ring 3) ----
    if (ring_count >= 3) {
        int16_t r3 = radius - 2 * (arc_width + ring_gap);
        if (r3 < 8) r3 = 8;
        st->arc_ring3 = gauge_create_arc(tile, cfg, cx, cy, r3, arc_width);
    }

    // ---- Tick marks (repeat across all active rings) ----
    if (cfg->tick_count > 0) {
        lv_color_t tick_color = resolve_lv_color(cfg->tick_color, 0x808080);
        uint16_t total_tick_lines = (uint16_t)cfg->tick_count * ring_count;
        st->tick_lines = (lv_obj_t**)lv_malloc(sizeof(lv_obj_t*) * total_tick_lines);
        if (!st->tick_lines) {
            LOGW(TAG, "Failed to alloc tick_lines (%u)", total_tick_lines);
        }

        gauge_create_ticks_for_radius(tile, st, cfg, cx, cy, radius, arc_width, tick_color);

        if (ring_count >= 2) {
            int16_t r2 = radius - arc_width - ring_gap;
            if (r2 < 8) r2 = 8;
            gauge_create_ticks_for_radius(tile, st, cfg, cx, cy, r2, arc_width, tick_color);
        }

        if (ring_count >= 3) {
            int16_t r3 = radius - 2 * (arc_width + ring_gap);
            if (r3 < 8) r3 = 8;
            gauge_create_ticks_for_radius(tile, st, cfg, cx, cy, r3, arc_width, tick_color);
        }
    }

    // ---- Needle ----
    if (cfg->show_needle && cfg->needle_width > 0) {
        st->n_pts = (lv_point_precise_t*)lv_malloc(sizeof(lv_point_precise_t) * 2);
        if (st->n_pts) {
            st->needle = lv_line_create(tile);
            lv_obj_set_style_line_color(st->needle, resolve_lv_color(cfg->needle_color, 0xFFFFFF), 0);
            lv_obj_set_style_line_width(st->needle, cfg->needle_width, 0);
            lv_obj_clear_flag(st->needle, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

            // Initial position at start angle (min value)
            float init_rad = (float)cfg->start_angle * (float)M_PI / 180.0f;
            gauge_set_needle(st->needle, st->n_pts, cx, cy, st->needle_len, init_rad);
        }
    }

    // ---- Reposition icon and center label to the needle pivot (cx, cy) ----
    // The icon_img passed by PadScreen is either the lv_image icon or the
    // center label (if no icon). Identify both objects so we can stack them
    // centered on the needle origin.
    lv_obj_t* real_icon = nullptr;
    lv_obj_t* center_lbl = nullptr;

    if (icon_img && lv_obj_check_type(icon_img, &lv_image_class)) {
        real_icon = icon_img;
        // Scan children to find the center label (uses larger font than top/bottom)
        uint32_t cnt = lv_obj_get_child_count(tile);
        for (uint32_t i = 0; i < cnt; i++) {
            lv_obj_t* child = lv_obj_get_child(tile, i);
            if (child == real_icon || child == st->arc_bg || child == st->arc_ring2 || child == st->arc_ring3) continue;
            if (!lv_obj_check_type(child, &lv_label_class)) continue;
            const lv_font_t* f = lv_obj_get_style_text_font(child, LV_PART_MAIN);
            if (f != scale->font_small) {
                center_lbl = child;
                break;
            }
        }
    } else if (icon_img && lv_obj_check_type(icon_img, &lv_label_class)) {
        center_lbl = icon_img;
    }

    // Total height of stacked elements (icon + label) so we can center them
    int16_t stack_h = 0;
    int16_t icon_h = 0, icon_w = 0, lbl_total_h = 0;
    if (real_icon) {
        lv_obj_update_layout(real_icon);
        icon_h = (int16_t)lv_obj_get_height(real_icon);
        icon_w = (int16_t)lv_obj_get_width(real_icon);
        stack_h += icon_h;
    }
    if (center_lbl) {
        lv_obj_update_layout(center_lbl);
        lbl_total_h = (int16_t)lv_obj_get_height(center_lbl);
        if (real_icon) stack_h += 2; // gap between icon and label
        stack_h += lbl_total_h;
    }

    // Center the stack on (cx, cy) — the needle pivot point
    int16_t stack_top = cy - stack_h / 2;

    if (real_icon) {
        // Use lv_obj_align to override PadScreen's stored LV_ALIGN_CENTER;
        // lv_obj_set_pos alone does NOT clear the alignment and LVGL may
        // re-apply the old CENTER alignment on subsequent layout updates.
        lv_obj_align(real_icon, LV_ALIGN_TOP_LEFT, cx - icon_w / 2, stack_top);
        lv_obj_move_foreground(real_icon);
    }

    if (center_lbl) {
        // Constrain label width to fit inside the innermost ring
        int16_t inner_r = radius - ring_count * arc_width - (ring_count - 1) * ring_gap;
        int16_t lbl_max_w = (int16_t)(inner_r * 1.4f); // ~70% of diameter
        if (lbl_max_w > content_w - 8) lbl_max_w = content_w - 8;
        if (lbl_max_w < 30) lbl_max_w = 30;
        lv_obj_set_width(center_lbl, lbl_max_w);
        lv_obj_set_style_text_align(center_lbl, LV_TEXT_ALIGN_CENTER, 0);
        int16_t lbl_y = real_icon ? (stack_top + icon_h + 2) : (cy - lbl_total_h / 2);
        // Override PadScreen's LV_ALIGN_CENTER so LVGL won't revert
        // position when binding text triggers a layout update.
        lv_obj_align(center_lbl, LV_ALIGN_TOP_LEFT, cx - lbl_max_w / 2, lbl_y);
        lv_obj_move_foreground(center_lbl);
    }
}

// ---- Helper: update a single arc ring ----
static void gauge_update_ring(lv_obj_t* arc, const GaugeConfig* cfg,
                               float value, float* last_value,
                               const char* color_field, uint32_t color_default,
                               uint32_t* color_cache) {
    if (!arc) return;

    // Skip redundant updates
    if (!isnan(*last_value) && fabsf(value - *last_value) < 0.001f) return;
    *last_value = value;

    // Compute fill ratio
    float range = cfg->max_value - cfg->min_value;
    if (range <= 0.0f) range = 1.0f;
    float ratio = (value - cfg->min_value) / range;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    if (cfg->zero_centered) {
        // Arc fills from the zero point; negative values grow left, positive grow right
        float zero_ratio = (0.0f - cfg->min_value) / range;
        if (zero_ratio < 0.0f) zero_ratio = 0.0f;
        if (zero_ratio > 1.0f) zero_ratio = 1.0f;
        float zero_angle = zero_ratio * (float)cfg->arc_degrees;
        float val_angle  = ratio * (float)cfg->arc_degrees;
        int32_t a_start = (int32_t)roundf(fminf(zero_angle, val_angle));
        int32_t a_end   = (int32_t)roundf(fmaxf(zero_angle, val_angle));
        lv_arc_set_angles(arc, a_start, a_end);
    } else {
        // Normal: fill from start edge
        float fill_angle = ratio * (float)cfg->arc_degrees;
        int32_t fill_angle_int = (int32_t)roundf(fill_angle);
        lv_arc_set_angles(arc, 0, fill_angle_int);
    }

    // Apply indicator color (uses shared cache with tick)
    lv_color_t clr;
    if (resolve_color_changed(color_field, color_default, color_cache, &clr))
        lv_obj_set_style_arc_color(arc, clr, LV_PART_INDICATOR);
}

static void gauge_update(lv_obj_t* tile, const WidgetConfig* wcfg,
                          WidgetState* state, const char* raw_value) {
    auto* cfg = reinterpret_cast<const GaugeConfig*>(wcfg->data);
    auto* st = reinterpret_cast<GaugeState*>(state->data);

    if (!st->arc_bg) return;

    // raw_value may be "val1\tval2\tval3" for multi-ring gauges.
    // Split on '\t' and update each ring independently.
    const char* p = raw_value;
    const char* vals[3] = { p, nullptr, nullptr };
    for (int i = 1; i < 3; i++) {
        const char* tab = strchr(p, '\t');
        if (!tab) break;
        vals[i] = tab + 1;
        p = tab + 1;
    }

    // Ring 1 (outer / primary)
    {
        char* end = nullptr;
        float value = strtof(vals[0], &end);
        if (end != vals[0]) {
            float prev = st->last_value;
            gauge_update_ring(st->arc_bg, cfg, value, &st->last_value, cfg->arc_color, 0x4CAF50, &st->cached_arc1);

            // Update needle for outer ring only
            if (st->needle && st->n_pts && cfg->show_needle && cfg->needle_width > 0
                && (isnan(prev) || fabsf(value - prev) >= 0.001f)) {
                float range = cfg->max_value - cfg->min_value;
                if (range <= 0.0f) range = 1.0f;
                float ratio = (value - cfg->min_value) / range;
                if (ratio < 0.0f) ratio = 0.0f;
                if (ratio > 1.0f) ratio = 1.0f;
                int32_t fill_angle_int = (int32_t)roundf(ratio * (float)cfg->arc_degrees);
                float a_rad = ((float)cfg->start_angle + (float)fill_angle_int) * (float)M_PI / 180.0f;
                gauge_set_needle(st->needle, st->n_pts, st->cx, st->cy, st->needle_len, a_rad);
            }
        }
    }

    // Ring 2 (middle)
    if (vals[1] && st->arc_ring2) {
        char* end = nullptr;
        float value = strtof(vals[1], &end);
        if (end != vals[1]) {
            gauge_update_ring(st->arc_ring2, cfg, value, &st->last_value_2, cfg->arc_color_2, 0x2196F3, &st->cached_arc2);
        }
    }

    // Ring 3 (inner)
    if (vals[2] && st->arc_ring3) {
        char* end = nullptr;
        float value = strtof(vals[2], &end);
        if (end != vals[2]) {
            gauge_update_ring(st->arc_ring3, cfg, value, &st->last_value_3, cfg->arc_color_3, 0x9C27B0, &st->cached_arc3);
        }
    }

}

// ---- Tick: re-resolve binding-driven colors (skip if unchanged) ----

static void gauge_tick(lv_obj_t* tile, const WidgetConfig* wcfg,
                       WidgetState* state) {
    auto* cfg = reinterpret_cast<const GaugeConfig*>(wcfg->data);
    auto* st = reinterpret_cast<GaugeState*>(state->data);
    if (!st->arc_bg) return;

    lv_color_t clr;

    // Track color (background arc)
    if (resolve_color_changed(cfg->track_color, 0x1A1A1A, &st->cached_track, &clr)) {
        lv_obj_set_style_arc_color(st->arc_bg, clr, LV_PART_MAIN);
        if (st->arc_ring2) lv_obj_set_style_arc_color(st->arc_ring2, clr, LV_PART_MAIN);
        if (st->arc_ring3) lv_obj_set_style_arc_color(st->arc_ring3, clr, LV_PART_MAIN);
    }

    // Needle color
    if (st->needle) {
        if (resolve_color_changed(cfg->needle_color, 0xFFFFFF, &st->cached_needle, &clr))
            lv_obj_set_style_line_color(st->needle, clr, 0);
    }

    // Tick mark color (uses cached tick line pointers — no child scan)
    if (st->tick_line_count > 0) {
        if (resolve_color_changed(cfg->tick_color, 0x808080, &st->cached_tick, &clr)) {
            for (uint8_t i = 0; i < st->tick_line_count; i++)
                lv_obj_set_style_line_color(st->tick_lines[i], clr, 0);
        }
    }

    // Indicator colors
    if (resolve_color_changed(cfg->arc_color, 0x4CAF50, &st->cached_arc1, &clr))
        lv_obj_set_style_arc_color(st->arc_bg, clr, LV_PART_INDICATOR);
    if (st->arc_ring2) {
        if (resolve_color_changed(cfg->arc_color_2, 0x2196F3, &st->cached_arc2, &clr))
            lv_obj_set_style_arc_color(st->arc_ring2, clr, LV_PART_INDICATOR);
    }
    if (st->arc_ring3) {
        if (resolve_color_changed(cfg->arc_color_3, 0x9C27B0, &st->cached_arc3, &clr))
            lv_obj_set_style_arc_color(st->arc_ring3, clr, LV_PART_INDICATOR);
    }
}

static void gauge_destroy(WidgetState* state) {
    auto* st = reinterpret_cast<GaugeState*>(state->data);
    // Free heap-allocated needle points
    if (st->n_pts) {
        lv_free(st->n_pts);
        st->n_pts = nullptr;
    }
    // Free heap-allocated tick line pointer array
    if (st->tick_lines) {
        lv_free(st->tick_lines);
        st->tick_lines = nullptr;
    }
    // LVGL child objects (arc, ticks, needle line) are deleted automatically
    // when the tile is deleted. Tick points are freed via LV_EVENT_DELETE callbacks.
}

// ---- Registration ----

REGISTER_WIDGET(gauge, nullptr);

#endif // HAS_DISPLAY
