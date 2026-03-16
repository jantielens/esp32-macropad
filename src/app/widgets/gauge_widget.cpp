#include "widget.h"

#if HAS_DISPLAY

#include "../binding_template.h"
#include "../log_manager.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TAG "Gauge"
#define GAUGE_START_LABEL_CONNECTOR_LEN 8

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
    char     min_value[CONFIG_BINDABLE_SHORT_LEN];  // Scale minimum (bindable, default "0")
    char     max_value[CONFIG_BINDABLE_SHORT_LEN];  // Scale maximum (bindable, default "100")
    uint16_t arc_degrees;            // Arc span (10–360, default 180)
    uint16_t start_angle;            // Rotation offset in degrees (default 180)
    char     start_label[CONFIG_BINDABLE_SHORT_LEN];       // Outer ring start label (plain text or binding)
    char     start_label_2[CONFIG_BINDABLE_SHORT_LEN];     // Slot 2 start label (plain text or binding)
    char     start_label_3[CONFIG_BINDABLE_SHORT_LEN];     // Slot 3 start label (plain text or binding)
    char     start_label_4[CONFIG_BINDABLE_SHORT_LEN];     // 4th ring start label (plain text or binding)
    char     arc_color[CONFIG_COLOR_MAX_LEN];               // Arc indicator color (bindable, default "#4CAF50")
    char     arc_color_2[CONFIG_COLOR_MAX_LEN];             // Ring 2 indicator color (bindable, default "#2196F3")
    char     arc_color_3[CONFIG_COLOR_MAX_LEN];             // Ring 3 indicator color (bindable, default "#9C27B0")
    char     arc_color_4[CONFIG_COLOR_MAX_LEN];             // Ring 4 indicator color (bindable, default "#FF9800")
    char     track_color[CONFIG_BINDABLE_SHORT_LEN];        // Inactive arc background (default "#1A1A1A")
    char     needle_color[CONFIG_BINDABLE_SHORT_LEN];       // Needle color (default "#FFFFFF")
    char     tick_color[CONFIG_BINDABLE_SHORT_LEN];          // Tick mark color (default "#808080")
    uint8_t  arc_width_pct;          // Arc thickness as % of radius (5–50, default 15)
    uint8_t  tick_count;             // Major tick count (0 = none, default 5)
    uint8_t  needle_width;           // Needle line width in pixels (1–10, default 2)
    uint8_t  tick_width;             // Tick line width in pixels (1–5, default 1)
    bool     show_needle;            // Draw a needle line (default true)
    bool     zero_centered;          // Arc fills from zero point instead of min (default false)
    bool     dual_binding_pair_1;    // Slots 1/2 share one ring (pos/neg)
    bool     dual_binding_pair_2;    // Slots 3/4 share one ring (pos/neg)
};

static_assert(sizeof(GaugeConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "GaugeConfig exceeds WIDGET_CONFIG_MAX_BYTES");

// ---- Runtime state (packed into WidgetState.data[]) ----

struct GaugeState {
    lv_obj_t* arc_bg;              // Outer ring arc (track + indicator)
    lv_obj_t* arc_ring2;           // Second visible ring arc (or nullptr)
    lv_obj_t* arc_ring3;           // Third visible ring arc (or nullptr)
    lv_obj_t* arc_ring4;           // 4th ring arc (or nullptr)
    lv_obj_t* arc_dual_1_neg;      // Outer ring negative indicator arc (dual pair 1)
    lv_obj_t* arc_dual_2_neg;      // Secondary ring negative indicator arc (dual pair 2)
    lv_obj_t* start_label_1;       // Outer ring start label (or nullptr)
    lv_obj_t* start_label_2;       // Second visible ring start label (or nullptr)
    lv_obj_t* start_label_3;       // Third visible ring start label (or nullptr)
    lv_obj_t* start_label_4;       // 4th ring start label (or nullptr)
    lv_obj_t* needle;              // Needle line object (or nullptr)
    lv_point_precise_t* n_pts;     // Heap-allocated needle points (2 elements)
    lv_obj_t** tick_lines;         // Heap-allocated tick line pointers (or nullptr)
    float     last_value;          // Last numeric value for outer ring
    float     last_value_2;        // Last numeric value for second visible ring
    float     last_value_3;        // Last numeric value for third visible ring
    float     last_value_4;        // Last numeric value for 4th ring
    float     last_value_neg_1;    // Last numeric value for outer negative ring (dual pair 1)
    float     last_value_neg_2;    // Last numeric value for secondary negative ring (dual pair 2)
    float     cached_min;           // Last resolved min (for detecting binding changes in tick)
    float     cached_max;           // Last resolved max (for detecting binding changes in tick)
    int16_t   cx, cy;              // Arc geometric center in tile coords
    int16_t   outer_radius;        // Outer ring radius in pixels
    int16_t   arc_width_px;        // Arc thickness in pixels
    int16_t   ring_gap_px;         // Gap between ring slots in pixels
    int16_t   needle_len;          // Needle length in pixels
    uint8_t   tick_line_count;     // Number of cached tick line pointers
    uint8_t   start_label_slot_1;  // Visual ring slot index for start_label_1
    uint8_t   start_label_slot_2;  // Visual ring slot index for start_label_2
    uint8_t   start_label_slot_3;  // Visual ring slot index for start_label_3
    uint8_t   start_label_slot_4;  // Visual ring slot index for start_label_4
    bool      dual_pair_1_active;  // Pair 1 rendered as one ring (slots 1/2)
    bool      dual_pair_2_active;  // Pair 2 rendered as one ring (slots 3/4)
    // Color cache (skip LVGL setters when unchanged)
    uint32_t  cached_track;        // track_color
    uint32_t  cached_needle;       // needle_color
    uint32_t  cached_tick;         // tick_color
    uint32_t  cached_arc1;         // arc_color (indicator)
    uint32_t  cached_arc2;         // arc_color_2 (indicator)
    uint32_t  cached_arc3;         // arc_color_3 (indicator)
    uint32_t  cached_arc4;         // arc_color_4 (indicator)
    uint32_t  cached_arc_neg1;     // arc_color_2 in dual pair 1 (negative)
    uint32_t  cached_arc_neg2;     // arc_color_4 in dual pair 2 (negative)
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

static int16_t gauge_radius_for_slot(const GaugeState* st, uint8_t slot_index) {
    return st->outer_radius - (int16_t)slot_index * (st->arc_width_px + st->ring_gap_px);
}

static float gauge_normalize_angle(float angle_deg) {
    while (angle_deg >= 180.0f) angle_deg -= 360.0f;
    while (angle_deg < -180.0f) angle_deg += 360.0f;
    return angle_deg;
}

static float gauge_tangent_readable_angle(float ux, float uy, bool* flipped_for_readability) {
    // Label baseline follows the tangent direction in screen coordinates.
    // Keep text readable by flipping 180 deg when tangent points left.
    float angle = atan2f(uy, ux) * 180.0f / (float)M_PI;
    bool flipped = false;

    if (ux < 0.0f) {
        angle += 180.0f;
        flipped = true;
    }

    angle = gauge_normalize_angle(angle);
    if (flipped_for_readability) *flipped_for_readability = flipped;
    return angle;
}

static lv_obj_t* gauge_create_start_label(lv_obj_t* tile, const lv_font_t* font, lv_color_t color) {
    lv_obj_t* label = lv_label_create(tile);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, "");
    lv_obj_clear_flag(label, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_move_foreground(label);
    return label;
}

static void gauge_set_start_label_text(lv_obj_t* label, const char* templ) {
    if (!label || !templ) return;

    char resolved[CONFIG_BINDABLE_SHORT_LEN];
    resolved[0] = '\0';
#if HAS_MQTT
    if (binding_template_has_bindings(templ)) {
        binding_template_resolve(templ, resolved, sizeof(resolved));
    } else
#endif
    {
        strlcpy(resolved, templ, sizeof(resolved));
    }

    if (strcmp(lv_label_get_text(label), resolved) != 0) {
        lv_label_set_text(label, resolved);
    }

    if (resolved[0]) lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
}

static void gauge_place_start_label(lv_obj_t* label, const GaugeConfig* cfg,
                                    const GaugeState* st, uint8_t slot_index) {
    if (!label || lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN)) return;

    const int16_t radius = gauge_radius_for_slot(st, slot_index);
    const float angle_rad = (float)cfg->start_angle * (float)M_PI / 180.0f;
    float sx = st->cx + cosf(angle_rad) * radius;
    float sy = st->cy + sinf(angle_rad) * radius;

    sx -= cosf(angle_rad) * (st->arc_width_px / 2.0f);
    sy -= sinf(angle_rad) * (st->arc_width_px / 2.0f);

    const float ux = sinf(angle_rad);
    const float uy = -cosf(angle_rad);

    const float bx = sx + ux * GAUGE_START_LABEL_CONNECTOR_LEN;
    const float by = sy + uy * GAUGE_START_LABEL_CONNECTOR_LEN;

    bool flipped = false;
    const float label_angle = gauge_tangent_readable_angle(ux, uy, &flipped);

    lv_obj_update_layout(label);
    const lv_coord_t label_w = lv_obj_get_width(label);
    const lv_coord_t label_h = lv_obj_get_height(label);
    const lv_coord_t pivot_x = flipped ? label_w : 0;
    const lv_coord_t pivot_y = label_h / 2;

    // Ensure the full transformed label footprint is placed outward along the
    // connector tangent direction so glyphs don't overlap the ring.
    const float theta = label_angle * (float)M_PI / 180.0f;
    const float dx = cosf(theta);
    const float dy = sinf(theta);
    const float nx = -sinf(theta);
    const float ny = cosf(theta);

    const float x0 = flipped ? -(float)label_w : 0.0f;
    const float x1 = flipped ? 0.0f : (float)label_w;
    const float y0 = -(float)label_h / 2.0f;
    const float y1 = (float)label_h / 2.0f;

    const float du = dx * ux + dy * uy;
    const float nu = nx * ux + ny * uy;
    const float min_x_proj = (du >= 0.0f) ? (x0 * du) : (x1 * du);
    const float min_y_proj = (nu >= 0.0f) ? (y0 * nu) : (y1 * nu);
    const float min_proj = min_x_proj + min_y_proj;
    const float push_out = (min_proj < 0.0f) ? (-min_proj) : 0.0f;

    const float anchor_x = bx + ux * push_out;
    const float anchor_y = by + uy * push_out;

    lv_obj_set_style_transform_pivot_x(label, pivot_x, 0);
    lv_obj_set_style_transform_pivot_y(label, pivot_y, 0);
    lv_obj_set_style_transform_rotation(label, (int32_t)lroundf(label_angle * 10.0f), 0);
    lv_obj_set_pos(label,
                   (lv_coord_t)lroundf(anchor_x) - pivot_x,
                   (lv_coord_t)lroundf(anchor_y) - pivot_y);
}

static void gauge_update_start_label(lv_obj_t* label, const char* templ,
                                     const GaugeConfig* cfg, const GaugeState* st,
                                     uint8_t slot_index) {
    if (!label || !templ || !templ[0]) return;
    gauge_set_start_label_text(label, templ);
    gauge_place_start_label(label, cfg, st, slot_index);
}

// ---- WidgetType callbacks ----

static void gauge_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<GaugeConfig*>(data);
    memset(cfg, 0, sizeof(GaugeConfig));

    widget_parse_field(btn["widget_gauge_min"],  cfg->min_value,  sizeof(cfg->min_value),  "0", false);
    widget_parse_field(btn["widget_gauge_max"],  cfg->max_value,  sizeof(cfg->max_value),  "100", false);

    int deg = btn["widget_gauge_degrees"] | 180;
    cfg->arc_degrees = (uint16_t)clamp_val(deg, 10, 360);

    int sa = btn["widget_gauge_start_angle"] | 180;
    cfg->start_angle = (uint16_t)(sa % 360);

    cfg->show_needle     = btn["widget_gauge_show_needle"] | true;
    cfg->zero_centered   = btn["widget_gauge_zero_centered"] | false;
    cfg->dual_binding_pair_1 = btn["widget_gauge_dual_binding_pair_1"] | false;
    cfg->dual_binding_pair_2 = btn["widget_gauge_dual_binding_pair_2"] | false;

    widget_parse_field(btn["widget_gauge_start_label"],   cfg->start_label,   sizeof(cfg->start_label),   "", false);
    widget_parse_field(btn["widget_gauge_start_label_2"], cfg->start_label_2, sizeof(cfg->start_label_2), "", false);
    widget_parse_field(btn["widget_gauge_start_label_3"], cfg->start_label_3, sizeof(cfg->start_label_3), "", false);
    widget_parse_field(btn["widget_gauge_start_label_4"], cfg->start_label_4, sizeof(cfg->start_label_4), "", false);

    // Per-ring arc indicator colors (bindable — may contain threshold() expressions)
    widget_parse_field(btn["widget_arc_color"],   cfg->arc_color,   sizeof(cfg->arc_color),   "#4CAF50");
    widget_parse_field(btn["widget_arc_color_2"], cfg->arc_color_2, sizeof(cfg->arc_color_2), "#2196F3");
    widget_parse_field(btn["widget_arc_color_3"], cfg->arc_color_3, sizeof(cfg->arc_color_3), "#9C27B0");
    widget_parse_field(btn["widget_arc_color_4"], cfg->arc_color_4, sizeof(cfg->arc_color_4), "#FF9800");

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
    st->last_value_4 = NAN;
    st->last_value_neg_1 = NAN;
    st->last_value_neg_2 = NAN;
    st->cached_min = NAN;
    st->cached_max = NAN;
    st->tick_line_count = 0;
    st->cached_track  = COLOR_CACHE_INIT;
    st->cached_needle = COLOR_CACHE_INIT;
    st->cached_tick   = COLOR_CACHE_INIT;
    st->cached_arc1   = COLOR_CACHE_INIT;
    st->cached_arc2   = COLOR_CACHE_INIT;
    st->cached_arc3   = COLOR_CACHE_INIT;
    st->cached_arc4   = COLOR_CACHE_INIT;
    st->cached_arc_neg1 = COLOR_CACHE_INIT;
    st->cached_arc_neg2 = COLOR_CACHE_INIT;

    // Slot occupancy: slot1..4 map to data_binding[0..3].
    const bool has_slot2 = wcfg->data_binding[1][0];
    const bool has_slot3 = wcfg->data_binding[2][0];
    const bool has_slot4 = wcfg->data_binding[3][0];

    // Pair collapse (dual binding):
    // pair 1 = slot1(+),slot2(-); pair 2 = slot3(+),slot4(-).
    st->dual_pair_1_active = cfg->dual_binding_pair_1 && has_slot2;
    st->dual_pair_2_active = cfg->dual_binding_pair_2 && has_slot4;

    uint8_t active_ring_count = 1;
    if (!st->dual_pair_1_active && has_slot2) active_ring_count++;
    if (has_slot3) active_ring_count++;
    if (has_slot4 && !st->dual_pair_2_active) active_ring_count++;

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
    cx += btn->ui_offset_x;
    cy += btn->ui_offset_y;

    st->cx = cx;
    st->cy = cy;
    st->outer_radius = radius;
    st->arc_width_px = arc_width;
    // Needle extends from center outward past the arc outer edge
    st->needle_len = radius + 4;
    if (st->needle_len < 10) st->needle_len = 10;

    LOGD(TAG, "Layout: rect=%dx%d avail=%dx%d radius=%d arc_w=%d cx=%d cy=%d rings=%d",
            rect->w, rect->h, avail_w, avail_h, radius, arc_width, cx, cy, active_ring_count);

    // ---- Gap between concentric rings ----
    int16_t ring_gap = (arc_width >= 8) ? (arc_width / 4) : 2;
    st->ring_gap_px = ring_gap;

    // ---- Create rings from slot model ----
    uint8_t ring_slot = 0;
    int16_t r_cur = radius;

    // Ring 1 (slot 1, outer)
    st->arc_bg = gauge_create_arc(tile, cfg, cx, cy, r_cur, arc_width);
    if (st->dual_pair_1_active) {
        st->arc_dual_1_neg = gauge_create_arc(tile, cfg, cx, cy, r_cur, arc_width);
        // Dual overlay arc should only draw indicator, never an extra track layer.
        lv_obj_set_style_arc_opa(st->arc_dual_1_neg, LV_OPA_TRANSP, LV_PART_MAIN);
    }
    if (cfg->start_label[0]) {
        st->start_label_1 = gauge_create_start_label(tile, scale->font_small,
                                                     resolve_lv_color(cfg->arc_color, 0x4CAF50));
        st->start_label_slot_1 = ring_slot;
        gauge_update_start_label(st->start_label_1, cfg->start_label, cfg, st, ring_slot);
    }
    ring_slot++;
    r_cur -= (arc_width + ring_gap);
    if (r_cur < 8) r_cur = 8;

    // Ring 2 is either slot 2 (normal) or slot 3 (when pair 1 is dual)
    if (!st->dual_pair_1_active && has_slot2) {
        st->arc_ring2 = gauge_create_arc(tile, cfg, cx, cy, r_cur, arc_width);
        if (cfg->start_label_2[0]) {
            st->start_label_2 = gauge_create_start_label(tile, scale->font_small,
                                                         resolve_lv_color(cfg->arc_color_2, 0x2196F3));
            st->start_label_slot_2 = ring_slot;
            gauge_update_start_label(st->start_label_2, cfg->start_label_2, cfg, st, ring_slot);
        }
        ring_slot++;
        r_cur -= (arc_width + ring_gap);
        if (r_cur < 8) r_cur = 8;
    }

    if (has_slot3) {
        if (!st->dual_pair_1_active) {
            // slot3 is ring3 when pair1 is not dual
            st->arc_ring3 = gauge_create_arc(tile, cfg, cx, cy, r_cur, arc_width);
            if (st->dual_pair_2_active) {
                st->arc_dual_2_neg = gauge_create_arc(tile, cfg, cx, cy, r_cur, arc_width);
                // Dual overlay arc should only draw indicator, never an extra track layer.
                lv_obj_set_style_arc_opa(st->arc_dual_2_neg, LV_OPA_TRANSP, LV_PART_MAIN);
            }
            if (cfg->start_label_3[0]) {
                st->start_label_3 = gauge_create_start_label(tile, scale->font_small,
                                                             resolve_lv_color(cfg->arc_color_3, 0x9C27B0));
                st->start_label_slot_3 = ring_slot;
                gauge_update_start_label(st->start_label_3, cfg->start_label_3, cfg, st, ring_slot);
            }
            ring_slot++;
            r_cur -= (arc_width + ring_gap);
            if (r_cur < 8) r_cur = 8;
        } else {
            // slot3 becomes ring2 when pair1 is dual
            st->arc_ring2 = gauge_create_arc(tile, cfg, cx, cy, r_cur, arc_width);
            if (st->dual_pair_2_active) {
                st->arc_dual_2_neg = gauge_create_arc(tile, cfg, cx, cy, r_cur, arc_width);
                // Dual overlay arc should only draw indicator, never an extra track layer.
                lv_obj_set_style_arc_opa(st->arc_dual_2_neg, LV_OPA_TRANSP, LV_PART_MAIN);
            }
            if (cfg->start_label_3[0]) {
                st->start_label_2 = gauge_create_start_label(tile, scale->font_small,
                                                             resolve_lv_color(cfg->arc_color_3, 0x9C27B0));
                st->start_label_slot_2 = ring_slot;
                gauge_update_start_label(st->start_label_2, cfg->start_label_3, cfg, st, ring_slot);
            }
            ring_slot++;
            r_cur -= (arc_width + ring_gap);
            if (r_cur < 8) r_cur = 8;
        }
    }

    // slot4 is only a standalone ring when pair2 is not dual
    if (has_slot4 && !st->dual_pair_2_active) {
        if (st->dual_pair_1_active) {
            st->arc_ring3 = gauge_create_arc(tile, cfg, cx, cy, r_cur, arc_width);
            if (cfg->start_label_4[0]) {
                st->start_label_3 = gauge_create_start_label(tile, scale->font_small,
                                                             resolve_lv_color(cfg->arc_color_4, 0xFF9800));
                st->start_label_slot_3 = ring_slot;
                gauge_update_start_label(st->start_label_3, cfg->start_label_4, cfg, st, ring_slot);
            }
        } else {
            st->arc_ring4 = gauge_create_arc(tile, cfg, cx, cy, r_cur, arc_width);
            if (cfg->start_label_4[0]) {
                st->start_label_4 = gauge_create_start_label(tile, scale->font_small,
                                                             resolve_lv_color(cfg->arc_color_4, 0xFF9800));
                st->start_label_slot_4 = ring_slot;
                gauge_update_start_label(st->start_label_4, cfg->start_label_4, cfg, st, ring_slot);
            }
        }
    }

    // ---- Tick marks (repeat across all active rings) ----
    if (cfg->tick_count > 0) {
        lv_color_t tick_color = resolve_lv_color(cfg->tick_color, 0x808080);
        uint16_t total_tick_lines = (uint16_t)cfg->tick_count * active_ring_count;
        st->tick_lines = (lv_obj_t**)lv_malloc(sizeof(lv_obj_t*) * total_tick_lines);
        if (!st->tick_lines) {
            LOGW(TAG, "Failed to alloc tick_lines (%u)", total_tick_lines);
        }

        for (uint8_t i = 0; i < active_ring_count; i++) {
            int16_t rr = radius - (int16_t)i * (arc_width + ring_gap);
            if (rr < 8) rr = 8;
            gauge_create_ticks_for_radius(tile, st, cfg, cx, cy, rr, arc_width, tick_color);
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
            if (child == real_icon || child == st->arc_bg || child == st->arc_ring2 || child == st->arc_ring3
                || child == st->arc_ring4 || child == st->arc_dual_1_neg || child == st->arc_dual_2_neg) continue;
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
        // Disable center-label clipping for gauge tiles by giving the label a
        // wide draw area. This keeps CLIP-mode labels rendering reliably even
        // when center text is updated dynamically from bindings.
        lv_obj_add_flag(tile, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_add_flag(center_lbl, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        int16_t lbl_draw_w = content_w * 3;
        if (lbl_draw_w < 30) lbl_draw_w = 30;
        lv_obj_set_width(center_lbl, lbl_draw_w);
        lv_obj_set_style_text_align(center_lbl, LV_TEXT_ALIGN_CENTER, 0);
        int16_t lbl_y = (real_icon ? (stack_top + icon_h + 2) : (cy - lbl_total_h / 2))
                        + btn->style_center.y_offset;
        // Override PadScreen's LV_ALIGN_CENTER so LVGL won't revert
        // position when binding text triggers a layout update.
        // ui_offset_x/y are already absorbed into cx/cy; only add style offsets here.
        lv_obj_align(center_lbl, LV_ALIGN_TOP_LEFT,
                     cx - lbl_draw_w / 2 + btn->style_center.x_offset, lbl_y);
        lv_obj_move_foreground(center_lbl);
    }
}

// ---- Helper: update a single arc ring ----
static void gauge_update_ring(lv_obj_t* arc, const GaugeConfig* cfg,
                               float min_val, float max_val,
                               float value, float* last_value,
                               const char* color_field, uint32_t color_default,
                               uint32_t* color_cache, lv_obj_t* start_label,
                               bool force_zero_centered = false) {
    if (!arc) return;

    // Skip redundant updates
    if (!isnan(*last_value) && fabsf(value - *last_value) < 0.001f) return;
    *last_value = value;

    // Compute fill ratio
    float range = max_val - min_val;
    if (range <= 0.0f) range = 1.0f;
    float ratio = (value - min_val) / range;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    if (cfg->zero_centered || force_zero_centered) {
        // Arc fills from the zero point; negative values grow left, positive grow right
        float zero_ratio = (0.0f - min_val) / range;
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
    if (resolve_color_changed(color_field, color_default, color_cache, &clr)) {
        lv_obj_set_style_arc_color(arc, clr, LV_PART_INDICATOR);
        if (start_label) lv_obj_set_style_text_color(start_label, clr, 0);
    }
}

static void gauge_update(lv_obj_t* tile, const WidgetConfig* wcfg,
                          WidgetState* state, const char* raw_value) {
    auto* cfg = reinterpret_cast<const GaugeConfig*>(wcfg->data);
    auto* st = reinterpret_cast<GaugeState*>(state->data);

    if (!st->arc_bg) return;

    // Resolve bindable min/max once per update
    float min_val = resolve_number(cfg->min_value, 0.0f);
    float max_val = resolve_number(cfg->max_value, 100.0f);
    st->cached_min = min_val;
    st->cached_max = max_val;

    // raw_value may be "v1\tv2\tv3\tv4" with empty intermediate slots preserved.
    char raw_copy[BINDING_TEMPLATE_MAX_LEN * MAX_WIDGET_BINDINGS + MAX_WIDGET_BINDINGS + 1];
    strlcpy(raw_copy, raw_value ? raw_value : "", sizeof(raw_copy));
    char* vals[MAX_WIDGET_BINDINGS] = { raw_copy, nullptr, nullptr, nullptr };
    char* cursor = raw_copy;
    for (int i = 1; i < MAX_WIDGET_BINDINGS; i++) {
        char* tab = strchr(cursor, '\t');
        if (!tab) break;
        *tab = '\0';
        vals[i] = tab + 1;
        cursor = vals[i];
    }
    for (int i = 1; i < MAX_WIDGET_BINDINGS; i++) {
        if (!vals[i]) vals[i] = const_cast<char*>("");
    }

    auto parse_slot = [&](int idx, bool* ok) {
        char* end = nullptr;
        float v = strtof(vals[idx], &end);
        bool parsed = (end != vals[idx]);
        if (ok) *ok = parsed;
        return parsed ? v : 0.0f;
    };

    bool ok0 = false, ok1 = false, ok2 = false, ok3 = false;
    float v0 = parse_slot(0, &ok0);
    float v1 = parse_slot(1, &ok1);
    float v2 = parse_slot(2, &ok2);
    float v3 = parse_slot(3, &ok3);

    // Ring 1 always maps to slot 1 (positive direction).
    if (ok0) {
        gauge_update_ring(st->arc_bg, cfg, min_val, max_val, v0, &st->last_value, cfg->arc_color, 0x4CAF50,
                          &st->cached_arc1, st->start_label_1,
                          st->dual_pair_1_active);
    }

    // Dual pair 1: slot 2 is the negative direction on the same ring.
    if (st->dual_pair_1_active && st->arc_dual_1_neg) {
        float neg_mag = ok1 ? v1 : 0.0f;
        if (neg_mag < 0.0f) neg_mag = 0.0f;
        gauge_update_ring(st->arc_dual_1_neg, cfg, min_val, max_val, -neg_mag, &st->last_value_neg_1,
                          cfg->arc_color_2, 0x2196F3, &st->cached_arc_neg1, nullptr, true);
    } else if (st->arc_ring2) {
        // Non-dual: slot 2 is its own ring.
        if (ok1) {
            gauge_update_ring(st->arc_ring2, cfg, min_val, max_val, v1, &st->last_value_2, cfg->arc_color_2, 0x2196F3,
                              &st->cached_arc2, st->start_label_2);
        }
    }

    // Slot 3 can be ring2 or ring3 depending on pair1 mode.
    if (st->dual_pair_1_active) {
        if (st->arc_ring2) {
            if (ok2) {
                gauge_update_ring(st->arc_ring2, cfg, min_val, max_val, v2, &st->last_value_2, cfg->arc_color_3, 0x9C27B0,
                                  &st->cached_arc3, st->start_label_2,
                                  st->dual_pair_2_active);
            }
        }
    } else {
        if (st->arc_ring3) {
            if (ok2) {
                gauge_update_ring(st->arc_ring3, cfg, min_val, max_val, v2, &st->last_value_3, cfg->arc_color_3, 0x9C27B0,
                                  &st->cached_arc3, st->start_label_3,
                                  st->dual_pair_2_active);
            }
        }
    }

    // Dual pair 2: slot 4 is negative direction for slot 3 ring.
    if (st->dual_pair_2_active && st->arc_dual_2_neg) {
        float neg_mag = ok3 ? v3 : 0.0f;
        if (neg_mag < 0.0f) neg_mag = 0.0f;
        gauge_update_ring(st->arc_dual_2_neg, cfg, min_val, max_val, -neg_mag, &st->last_value_neg_2,
                          cfg->arc_color_4, 0xFF9800, &st->cached_arc_neg2, nullptr, true);
    } else if (st->dual_pair_1_active) {
        if (st->arc_ring3) {
            if (ok3) {
                gauge_update_ring(st->arc_ring3, cfg, min_val, max_val, v3, &st->last_value_3, cfg->arc_color_4, 0xFF9800,
                                  &st->cached_arc4, st->start_label_3);
            }
        }
    } else {
        if (st->arc_ring4) {
            if (ok3) {
                gauge_update_ring(st->arc_ring4, cfg, min_val, max_val, v3, &st->last_value_4, cfg->arc_color_4, 0xFF9800,
                                  &st->cached_arc4, st->start_label_4);
            }
        }
    }

    // Needle uses signed difference in dual pair 1 mode, otherwise slot 1 value.
    float needle_value = v0;
    if (st->dual_pair_1_active) {
        float pos = v0;
        float neg = v1;
        if (pos < 0.0f) pos = 0.0f;
        if (neg < 0.0f) neg = 0.0f;
        needle_value = pos - neg;
    }

    if (ok0 && st->needle && st->n_pts && cfg->show_needle && cfg->needle_width > 0) {
        float range = max_val - min_val;
        if (range <= 0.0f) range = 1.0f;
        float ratio = (needle_value - min_val) / range;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        int32_t fill_angle_int = (int32_t)roundf(ratio * (float)cfg->arc_degrees);
        float a_rad = ((float)cfg->start_angle + (float)fill_angle_int) * (float)M_PI / 180.0f;
        gauge_set_needle(st->needle, st->n_pts, st->cx, st->cy, st->needle_len, a_rad);
    }

}

// ---- Tick: re-resolve binding-driven colors (skip if unchanged) ----

static void gauge_tick(lv_obj_t* tile, const WidgetConfig* wcfg,
                       WidgetState* state) {
    auto* cfg = reinterpret_cast<const GaugeConfig*>(wcfg->data);
    auto* st = reinterpret_cast<GaugeState*>(state->data);
    if (!st->arc_bg) return;

    // Re-resolve bindable min/max; invalidate last_values so next update re-applies
    float mn = resolve_number(cfg->min_value, 0.0f);
    float mx = resolve_number(cfg->max_value, 100.0f);
    if (mn != st->cached_min || mx != st->cached_max) {
        st->cached_min = mn;
        st->cached_max = mx;
        st->last_value = NAN;
        st->last_value_2 = NAN;
        st->last_value_3 = NAN;
        st->last_value_4 = NAN;
        st->last_value_neg_1 = NAN;
        st->last_value_neg_2 = NAN;
    }

    lv_color_t clr;

    // Track color (background arc)
    if (resolve_color_changed(cfg->track_color, 0x1A1A1A, &st->cached_track, &clr)) {
        lv_obj_set_style_arc_color(st->arc_bg, clr, LV_PART_MAIN);
        if (st->arc_ring2) lv_obj_set_style_arc_color(st->arc_ring2, clr, LV_PART_MAIN);
        if (st->arc_ring3) lv_obj_set_style_arc_color(st->arc_ring3, clr, LV_PART_MAIN);
        if (st->arc_ring4) lv_obj_set_style_arc_color(st->arc_ring4, clr, LV_PART_MAIN);
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
    if (st->start_label_1) {
        gauge_update_start_label(st->start_label_1, cfg->start_label, cfg, st, st->start_label_slot_1);
        lv_obj_set_style_text_color(st->start_label_1, lv_obj_get_style_arc_color(st->arc_bg, LV_PART_INDICATOR), 0);
    }
    if (st->arc_ring2) {
        const char* arc2_color = st->dual_pair_1_active ? cfg->arc_color_3 : cfg->arc_color_2;
        uint32_t arc2_def = st->dual_pair_1_active ? 0x9C27B0 : 0x2196F3;
        uint32_t* arc2_cache = st->dual_pair_1_active ? &st->cached_arc3 : &st->cached_arc2;
        if (resolve_color_changed(arc2_color, arc2_def, arc2_cache, &clr))
            lv_obj_set_style_arc_color(st->arc_ring2, clr, LV_PART_INDICATOR);
        if (st->start_label_2) {
            const char* sl2 = st->dual_pair_1_active ? cfg->start_label_3 : cfg->start_label_2;
            gauge_update_start_label(st->start_label_2, sl2, cfg, st, st->start_label_slot_2);
            lv_obj_set_style_text_color(st->start_label_2, lv_obj_get_style_arc_color(st->arc_ring2, LV_PART_INDICATOR), 0);
        }
    }
    if (st->arc_ring3) {
        const char* arc3_color = st->dual_pair_1_active ? cfg->arc_color_4 : cfg->arc_color_3;
        uint32_t arc3_def = st->dual_pair_1_active ? 0xFF9800 : 0x9C27B0;
        uint32_t* arc3_cache = st->dual_pair_1_active ? &st->cached_arc4 : &st->cached_arc3;
        if (resolve_color_changed(arc3_color, arc3_def, arc3_cache, &clr))
            lv_obj_set_style_arc_color(st->arc_ring3, clr, LV_PART_INDICATOR);
        if (st->start_label_3) {
            const char* sl3 = st->dual_pair_1_active ? cfg->start_label_4 : cfg->start_label_3;
            gauge_update_start_label(st->start_label_3, sl3, cfg, st, st->start_label_slot_3);
            lv_obj_set_style_text_color(st->start_label_3, lv_obj_get_style_arc_color(st->arc_ring3, LV_PART_INDICATOR), 0);
        }
    }
    if (st->arc_ring4) {
        if (resolve_color_changed(cfg->arc_color_4, 0xFF9800, &st->cached_arc4, &clr))
            lv_obj_set_style_arc_color(st->arc_ring4, clr, LV_PART_INDICATOR);
        if (st->start_label_4) {
            gauge_update_start_label(st->start_label_4, cfg->start_label_4, cfg, st, st->start_label_slot_4);
            lv_obj_set_style_text_color(st->start_label_4, lv_obj_get_style_arc_color(st->arc_ring4, LV_PART_INDICATOR), 0);
        }
    }
    if (st->arc_dual_1_neg) {
        if (resolve_color_changed(cfg->arc_color_2, 0x2196F3, &st->cached_arc_neg1, &clr))
            lv_obj_set_style_arc_color(st->arc_dual_1_neg, clr, LV_PART_INDICATOR);
    }
    if (st->arc_dual_2_neg) {
        if (resolve_color_changed(cfg->arc_color_4, 0xFF9800, &st->cached_arc_neg2, &clr))
            lv_obj_set_style_arc_color(st->arc_dual_2_neg, clr, LV_PART_INDICATOR);
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
