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
    uint32_t color_good_rgb;         // Color tier 0: below threshold 1
    uint32_t color_ok_rgb;           // Color tier 1: threshold 1 → 2
    uint32_t color_attention_rgb;    // Color tier 2: threshold 2 → 3
    uint32_t color_warning_rgb;      // Color tier 3: at/above threshold 3
    float    threshold_1;            // Breakpoint tier 0 → 1
    float    threshold_2;            // Breakpoint tier 1 → 2
    float    threshold_3;            // Breakpoint tier 2 → 3
    uint32_t track_color_rgb;        // Inactive arc background (default 0x1A1A1A)
    uint32_t needle_color_rgb;       // Needle color (default 0xFFFFFF)
    uint32_t tick_color_rgb;          // Tick mark color (default 0x808080)
    uint8_t  arc_width_pct;          // Arc thickness as % of radius (5–50, default 15)
    uint8_t  tick_count;             // Major tick count (0 = none, default 5)
    uint8_t  needle_width;           // Needle line width in pixels (1–10, default 2)
    uint8_t  tick_width;             // Tick line width in pixels (1–5, default 1)
    bool     use_absolute;           // Compare |value| for color thresholds
    bool     show_needle;            // Draw a needle line (default true)
};

static_assert(sizeof(GaugeConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "GaugeConfig exceeds WIDGET_CONFIG_MAX_BYTES");

// ---- Runtime state (packed into WidgetState.data[]) ----

struct GaugeState {
    lv_obj_t* arc_bg;              // Background arc (track + indicator)
    lv_obj_t* needle;              // Needle line object (or nullptr)
    lv_point_precise_t* n_pts;     // Heap-allocated needle points (2 elements)
    float     last_value;          // Last numeric value (for skipping redundant updates)
    int16_t   cx, cy;              // Arc geometric center in tile coords
    int16_t   needle_len;          // Needle length in pixels
    uint16_t  pad;
};

static_assert(sizeof(GaugeState) <= WIDGET_STATE_MAX_BYTES,
              "GaugeState exceeds WIDGET_STATE_MAX_BYTES");

// ---- Helper: pick color tier based on value ----

static lv_color_t gauge_pick_tier_color(const GaugeConfig* cfg, float value) {
    float cmp = cfg->use_absolute ? fabsf(value) : value;
    uint32_t rgb;
    if (cmp >= cfg->threshold_3) rgb = cfg->color_warning_rgb;
    else if (cmp >= cfg->threshold_2) rgb = cfg->color_attention_rgb;
    else if (cmp >= cfg->threshold_1) rgb = cfg->color_ok_rgb;
    else rgb = cfg->color_good_rgb;
    return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// ---- Parse color helper (same logic as bar chart) ----

static uint32_t gauge_parse_color(JsonVariant v, uint32_t def) {
    if (v.isNull()) return def;
    if (v.is<unsigned long>() || v.is<long>()) return (uint32_t)v.as<unsigned long>();
    if (!v.is<const char*>()) return def;
    const char* s = v.as<const char*>();
    if (!s || !*s) return def;
    if (s[0] == '#') s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    char* end = nullptr;
    uint32_t val = strtoul(s, &end, 16);
    return (end == s) ? def : val;
}

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

// ---- WidgetType callbacks ----

static void gauge_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<GaugeConfig*>(data);
    memset(cfg, 0, sizeof(GaugeConfig));

    cfg->min_value  = btn["widget_gauge_min"] | 0.0f;
    cfg->max_value  = btn["widget_gauge_max"] | 100.0f;

    int deg = btn["widget_gauge_degrees"] | 180;
    cfg->arc_degrees = (uint16_t)((deg < 10) ? 10 : (deg > 360) ? 360 : deg);

    int sa = btn["widget_gauge_start_angle"] | 180;
    cfg->start_angle = (uint16_t)(sa % 360);

    cfg->use_absolute = btn["widget_use_absolute"] | true;
    cfg->show_needle  = btn["widget_gauge_show_needle"] | true;

    // Color tiers (same keys as bar chart for consistency)
    cfg->color_good_rgb      = gauge_parse_color(btn["widget_color_good"],      0x4CAF50);
    cfg->color_ok_rgb        = gauge_parse_color(btn["widget_color_ok"],        0x8BC34A);
    cfg->color_attention_rgb = gauge_parse_color(btn["widget_color_attention"], 0xFF9800);
    cfg->color_warning_rgb   = gauge_parse_color(btn["widget_color_warning"],   0xF44336);

    cfg->track_color_rgb  = gauge_parse_color(btn["widget_gauge_track_color"],  0x1A1A1A);
    cfg->needle_color_rgb = gauge_parse_color(btn["widget_gauge_needle_color"], 0xFFFFFF);
    cfg->tick_color_rgb   = gauge_parse_color(btn["widget_gauge_tick_color"],   0x808080);

    uint8_t awpct = btn["widget_gauge_arc_width_pct"] | (uint8_t)15;
    cfg->arc_width_pct = (awpct < 5) ? 5 : (awpct > 50) ? 50 : awpct;

    uint8_t tc = btn["widget_gauge_ticks"] | (uint8_t)5;
    cfg->tick_count = (tc > 20) ? 20 : tc;

    uint8_t nw = btn["widget_gauge_needle_width"] | (uint8_t)2;
    cfg->needle_width = (nw > 10) ? 10 : nw;  // 0 = no needle

    uint8_t tw = btn["widget_gauge_tick_width"] | (uint8_t)1;
    cfg->tick_width = (tw < 1) ? 1 : (tw > 5) ? 5 : tw;

    // Thresholds default to even thirds of range
    float range = cfg->max_value - cfg->min_value;
    cfg->threshold_1 = btn["widget_threshold_1"] | (cfg->min_value + range * 0.33f);
    cfg->threshold_2 = btn["widget_threshold_2"] | (cfg->min_value + range * 0.66f);
    cfg->threshold_3 = btn["widget_threshold_3"] | (cfg->min_value + range * 0.90f);
}

static void gauge_create(lv_obj_t* tile, const WidgetConfig* wcfg,
                          const ScreenButtonConfig* btn,
                          const PadRect* rect, const UIScaleInfo* scale,
                          lv_obj_t* icon_img, WidgetState* state) {
    auto* cfg = reinterpret_cast<const GaugeConfig*>(wcfg->data);
    auto* st = reinterpret_cast<GaugeState*>(state->data);
    memset(st, 0, sizeof(GaugeState));
    st->last_value = NAN;

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

    LOGD(TAG, "Layout: rect=%dx%d avail=%dx%d radius=%d arc_w=%d cx=%d cy=%d needle=%d",
         rect->w, rect->h, avail_w, avail_h, radius, arc_width, cx, cy, st->needle_len);

    // ---- Create LVGL arc ----
    lv_obj_t* arc_bg = lv_arc_create(tile);
    lv_obj_set_size(arc_bg, radius * 2, radius * 2);
    lv_obj_set_pos(arc_bg, cx - radius, cy - radius);
    // Use rotation for the start angle and bg_angles(0, degrees) to avoid
    // LVGL's internal angle normalization (>360 wrapping) which breaks the
    // linear value-to-indicator-angle mapping.
    lv_arc_set_rotation(arc_bg, cfg->start_angle);
    lv_arc_set_bg_angles(arc_bg, 0, cfg->arc_degrees);
    // Do NOT use lv_arc_set_range / lv_arc_set_value — LVGL's lv_map() uses
    // integer division which introduces visible quantization error at large
    // radii.  Instead we set the indicator angles directly in gauge_update()
    // via lv_arc_set_angles() with our own float-to-int rounding.
    lv_arc_set_mode(arc_bg, LV_ARC_MODE_NORMAL);
    // Track (background)
    uint32_t trk_rgb = cfg->track_color_rgb;
    lv_obj_set_style_arc_color(arc_bg, lv_color_make((trk_rgb >> 16) & 0xFF, (trk_rgb >> 8) & 0xFF, trk_rgb & 0xFF), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_bg, arc_width, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_bg, false, LV_PART_MAIN);
    // Indicator (filled portion)
    lv_obj_set_style_arc_color(arc_bg, lv_color_make(0x4C, 0xAF, 0x50), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_bg, arc_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_bg, false, LV_PART_INDICATOR);
    // Ensure no theme padding offsets the arc content area
    lv_obj_set_style_pad_all(arc_bg, 0, LV_PART_MAIN);
    // Hide knob
    lv_obj_set_style_pad_all(arc_bg, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc_bg, LV_OPA_TRANSP, LV_PART_KNOB);
    // Non-interactive, transparent background
    lv_obj_clear_flag(arc_bg, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_bg_opa(arc_bg, LV_OPA_TRANSP, LV_PART_MAIN);

    st->arc_bg = arc_bg;

    // ---- Tick marks ----
    if (cfg->tick_count > 0) {
        uint32_t t_rgb = cfg->tick_color_rgb;
        lv_color_t tick_color = lv_color_make((t_rgb >> 16) & 0xFF, (t_rgb >> 8) & 0xFF, t_rgb & 0xFF);
        int16_t tick_outer = radius - 1;
        int16_t tick_inner = radius - arc_width + 1;
        for (uint8_t i = 1; i <= cfg->tick_count; i++) {
            float angle = (float)cfg->start_angle + (float)cfg->arc_degrees * i / (cfg->tick_count + 1);
            float a_rad = angle * (float)M_PI / 180.0f;
            float cos_a = cosf(a_rad);
            float sin_a = sinf(a_rad);

            int16_t x1 = cx + (int16_t)roundf(cos_a * tick_inner);
            int16_t y1 = cy + (int16_t)roundf(sin_a * tick_inner);
            int16_t x2 = cx + (int16_t)roundf(cos_a * tick_outer);
            int16_t y2 = cy + (int16_t)roundf(sin_a * tick_outer);

            gauge_create_line(tile, x1, y1, x2, y2,
                              tick_color, cfg->tick_width);
        }
    }

    // ---- Needle ----
    if (cfg->show_needle && cfg->needle_width > 0) {
        st->n_pts = (lv_point_precise_t*)lv_malloc(sizeof(lv_point_precise_t) * 2);
        if (st->n_pts) {
            st->needle = lv_line_create(tile);
            uint32_t n_rgb = cfg->needle_color_rgb;
            lv_obj_set_style_line_color(st->needle,
                lv_color_make((n_rgb >> 16) & 0xFF, (n_rgb >> 8) & 0xFF, n_rgb & 0xFF), 0);
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
            if (child == real_icon || child == arc_bg) continue;
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
        // Constrain label width to fit inside the arc
        int16_t inner_r = radius - arc_width;
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

static void gauge_update(lv_obj_t* tile, const WidgetConfig* wcfg,
                          WidgetState* state, const char* raw_value) {
    auto* cfg = reinterpret_cast<const GaugeConfig*>(wcfg->data);
    auto* st = reinterpret_cast<GaugeState*>(state->data);

    if (!st->arc_bg) return;

    // Parse numeric value
    char* end = nullptr;
    float value = strtof(raw_value, &end);
    if (end == raw_value) return;

    // Skip redundant updates
    if (!isnan(st->last_value) && fabsf(value - st->last_value) < 0.001f) return;
    st->last_value = value;

    // Compute fill ratio
    float range = cfg->max_value - cfg->min_value;
    if (range <= 0.0f) range = 1.0f;
    float ratio = (value - cfg->min_value) / range;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    // Compute indicator angle directly (bypasses LVGL's lv_map integer division)
    float fill_angle = ratio * (float)cfg->arc_degrees;
    int32_t fill_angle_int = (int32_t)roundf(fill_angle);
    lv_arc_set_angles(st->arc_bg, 0, fill_angle_int);

    // Pick color tier and apply to indicator
    lv_color_t color = gauge_pick_tier_color(cfg, value);
    lv_obj_set_style_arc_color(st->arc_bg, color, LV_PART_INDICATOR);

    // Update needle position — use the same integer angle for pixel-perfect alignment
    if (st->needle && st->n_pts && cfg->show_needle && cfg->needle_width > 0) {
        float a_rad = ((float)cfg->start_angle + (float)fill_angle_int) * (float)M_PI / 180.0f;
        gauge_set_needle(st->needle, st->n_pts, st->cx, st->cy, st->needle_len, a_rad);
    }
}

static void gauge_destroy(WidgetState* state) {
    auto* st = reinterpret_cast<GaugeState*>(state->data);
    // Free heap-allocated needle points
    if (st->n_pts) {
        lv_free(st->n_pts);
        st->n_pts = nullptr;
    }
    // LVGL child objects (arc, ticks, needle line) are deleted automatically
    // when the tile is deleted. Tick points are freed via LV_EVENT_DELETE callbacks.
}

// ---- Registration ----

const WidgetType gauge_widget_type = {
    "gauge",
    gauge_parse,
    gauge_create,
    gauge_update,
    gauge_destroy
};

#endif // HAS_DISPLAY
