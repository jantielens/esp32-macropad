#include "widget.h"

#if HAS_DISPLAY

#include "../data_stream.h"
#include "../log_manager.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TAG "Sparkline"

// ============================================================================
// Sparkline Widget
// ============================================================================
// Renders a mini trend line inside the button tile. Samples are resampled
// into fixed time-window slots; the X-axis is evenly spaced, the Y-axis
// maps to [min_val, max_val] (user-set or auto-scaled from observed data).
//
// Layout within the tile:
//   ┌────────────────────────────┐
//   │    label_top (title)       │
//   │         icon               │
//   │  ╭─────────────────────╮   │
//   │  │  ╱╲    ╱╲           │   │
//   │  │ ╱  ╲╱╱  ╲──        │   │  ← sparkline
//   │  ╰─────────────────────╯   │
//   │    label_bottom (val)      │
//   └────────────────────────────┘

// ---- Config struct (packed into WidgetConfig.data[]) ----

// Reference line pattern enumeration
#define REF_LINE_SOLID   0
#define REF_LINE_DOTTED  1  // 2px dash, 4px gap
#define REF_LINE_DASHED  2  // 6px dash, 4px gap

#define MAX_REF_LINES 3

struct SparklineConfig {
    float    min_val;              // Y-axis minimum (NAN = auto-scale)
    float    max_val;              // Y-axis maximum (NAN = auto-scale)
    float    threshold_1;          // Color tier breakpoints
    float    threshold_2;
    float    threshold_3;
    float    ref_line_y[MAX_REF_LINES];   // Reference line Y-values (NAN = disabled)
    char     color_good[CONFIG_BINDABLE_SHORT_LEN];       // Color tier 0 (default "#4CAF50")
    char     color_ok[CONFIG_BINDABLE_SHORT_LEN];         // Color tier 1 (default "#8BC34A")
    char     color_attention[CONFIG_BINDABLE_SHORT_LEN];  // Color tier 2 (default "#FF9800")
    char     color_warning[CONFIG_BINDABLE_SHORT_LEN];    // Color tier 3 (default "#F44336")
    char     line_color[CONFIG_BINDABLE_SHORT_LEN];       // Main line color (default "#4CAF50")
    char     line_color_2[CONFIG_BINDABLE_SHORT_LEN];     // Extra line 2 color
    char     line_color_3[CONFIG_BINDABLE_SHORT_LEN];     // Extra line 3 color
    char     min_label_color[CONFIG_BINDABLE_SHORT_LEN];  // Empty = follow line color
    char     max_label_color[CONFIG_BINDABLE_SHORT_LEN];  // Empty = follow line color
    char     ref_line_color[MAX_REF_LINES][CONFIG_BINDABLE_SHORT_LEN]; // Reference line colors
    uint16_t window_secs;          // Time window in seconds (e.g. 300 = 5 min)
    uint8_t  slot_count;           // Number of sample slots (default 60)
    uint8_t  line_width;           // Line thickness in pixels (default 2)
    uint8_t  marker_size_min;      // Min marker dot size (0 = off, default 5)
    uint8_t  marker_size_max;      // Max marker dot size (0 = off, default 5)
    uint8_t  current_dot_size;     // Current-value dot size at right edge (0 = off)
    uint8_t  ref_line_pattern[MAX_REF_LINES]; // REF_LINE_SOLID/DOTTED/DASHED
    bool     use_absolute;         // Compare |value| for color tiers
    bool     use_thresholds;       // true = color by current value tier
    bool     unified_autoscale;    // true = shared auto min/max across all lines
    bool     ref_in_view;          // true = expand auto-scale to include ref lines
    uint8_t  smooth_factor;         // Gaussian smoothing radius (0=off, 1-8 = kernel radius)
    char     min_fmt[16];          // Printf format for min label (e.g. "lo %.1f")
    char     max_fmt[16];          // Printf format for max label (e.g. "hi %.1f")
};

static_assert(sizeof(SparklineConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "SparklineConfig exceeds WIDGET_CONFIG_MAX_BYTES");

// ---- Runtime state (packed into WidgetState.data[]) ----
// The sparkline no longer owns its ring buffer — data collection is
// handled by the DataStream registry (data_stream.cpp) which runs
// independently of the active screen.  SparklineState just holds the
// LVGL rendering objects and handles to its data streams.

#define MAX_SPARKLINE_LINES 3

struct SparklineState {
    lv_obj_t*            lines[MAX_SPARKLINE_LINES];   // LVGL line objects
    lv_point_precise_t*  points[MAX_SPARKLINE_LINES];  // Heap-allocated point arrays
    uint8_t  slot_count;             // Copy from config for points array size
    uint8_t  line_count;             // 1–3 (auto-detected from bindings)
#if HAS_MQTT
    data_stream_handle_t ds_handles[MAX_SPARKLINE_LINES]; // Data stream handles
#endif
    lv_obj_t* dot_min;               // Min marker dot (tile child)
    lv_obj_t* dot_max;               // Max marker dot (tile child)
    lv_obj_t* lbl_min;               // Min value label (tile child)
    lv_obj_t* lbl_max;               // Max value label (tile child)
    lv_obj_t* dot_current[MAX_SPARKLINE_LINES]; // Current-value dot per line
    lv_obj_t* ref_line_objs[MAX_REF_LINES]; // Reference line LVGL objects
    lv_point_precise_t* ref_pts;     // Heap-allocated ref line point pairs (MAX_REF_LINES * 2)
};

static_assert(sizeof(SparklineState) <= WIDGET_STATE_MAX_BYTES,
              "SparklineState exceeds WIDGET_STATE_MAX_BYTES");

// ---- Color tier helper (with dynamic thresholds for auto-scale) ----

static lv_color_t pick_tier_color(const SparklineConfig* cfg,
                                  float auto_min, float auto_max,
                                  float value) {
    float cmp = cfg->use_absolute ? fabsf(value) : value;

    // Compute effective thresholds: use config values if explicit,
    // otherwise derive from the effective Y-axis range
    float t1 = cfg->threshold_1;
    float t2 = cfg->threshold_2;
    float t3 = cfg->threshold_3;
    if (isnan(t1) || isnan(t2) || isnan(t3)) {
        float lo = isnan(cfg->min_val) ? auto_min : cfg->min_val;
        float hi = isnan(cfg->max_val) ? auto_max : cfg->max_val;
        if (!isfinite(lo) || !isfinite(hi) || lo >= hi) { lo = 0.0f; hi = 1.0f; }
        float r = hi - lo;
        if (isnan(t1)) t1 = lo + r * 0.33f;
        if (isnan(t2)) t2 = lo + r * 0.66f;
        if (isnan(t3)) t3 = lo + r * 0.90f;
    }

    if (cmp >= t3) return resolve_lv_color(cfg->color_warning, 0xF44336);
    if (cmp >= t2) return resolve_lv_color(cfg->color_attention, 0xFF9800);
    if (cmp >= t1) return resolve_lv_color(cfg->color_ok, 0x8BC34A);
    return resolve_lv_color(cfg->color_good, 0x4CAF50);
}

// ---- WidgetType callbacks ----

static void sparkline_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<SparklineConfig*>(data);
    memset(cfg, 0, sizeof(SparklineConfig));

    // Min/max: use NAN sentinel for auto-scale when not provided
    JsonVariant vmin = btn["widget_sparkline_min"];
    JsonVariant vmax = btn["widget_sparkline_max"];
    cfg->min_val = vmin.isNull() ? NAN : vmin.as<float>();
    cfg->max_val = vmax.isNull() ? NAN : vmax.as<float>();

    cfg->window_secs = btn["widget_sparkline_window"] | (uint16_t)300;
    if (cfg->window_secs < 10) cfg->window_secs = 10;

    uint8_t sc = btn["widget_sparkline_slots"] | (uint8_t)60;
    cfg->slot_count = (sc < 2) ? 2 : sc;  // minimum 2 points for a line

    uint8_t lw = btn["widget_sparkline_line_width"] | (uint8_t)2;
    cfg->line_width = (lw < 1) ? 1 : lw;

    widget_parse_field(btn["widget_sparkline_line_color"], cfg->line_color, sizeof(cfg->line_color), "#4CAF50");
    widget_parse_field(btn["widget_sparkline_line_color_2"], cfg->line_color_2, sizeof(cfg->line_color_2), "#2196F3");
    widget_parse_field(btn["widget_sparkline_line_color_3"], cfg->line_color_3, sizeof(cfg->line_color_3), "#9C27B0");
    cfg->use_thresholds = btn["widget_sparkline_use_thresholds"] | false;
    cfg->use_absolute = btn["widget_use_absolute"] | false;
    cfg->unified_autoscale = btn["widget_sparkline_unified_scale"] | true;  // default on

    // Color tiers (same defaults as bar chart)
    widget_parse_field(btn["widget_color_good"],      cfg->color_good,      sizeof(cfg->color_good),      "#4CAF50");
    widget_parse_field(btn["widget_color_ok"],        cfg->color_ok,        sizeof(cfg->color_ok),        "#8BC34A");
    widget_parse_field(btn["widget_color_attention"], cfg->color_attention, sizeof(cfg->color_attention), "#FF9800");
    widget_parse_field(btn["widget_color_warning"],   cfg->color_warning,   sizeof(cfg->color_warning),   "#F44336");

    // Thresholds: NAN = auto (computed dynamically from effective range)
    JsonVariant vt1 = btn["widget_threshold_1"];
    JsonVariant vt2 = btn["widget_threshold_2"];
    JsonVariant vt3 = btn["widget_threshold_3"];
    cfg->threshold_1 = vt1.isNull() ? NAN : vt1.as<float>();
    cfg->threshold_2 = vt2.isNull() ? NAN : vt2.as<float>();
    cfg->threshold_3 = vt3.isNull() ? NAN : vt3.as<float>();

    // Min/max markers
    cfg->marker_size_min = btn["widget_sparkline_marker_size_min"] | (uint8_t)0;
    cfg->marker_size_max = btn["widget_sparkline_marker_size_max"] | (uint8_t)0;

    // Min/max label colors (empty = follow line color)
    widget_parse_field(btn["widget_sparkline_min_label_color"], cfg->min_label_color, sizeof(cfg->min_label_color), "");
    widget_parse_field(btn["widget_sparkline_max_label_color"], cfg->max_label_color, sizeof(cfg->max_label_color), "");

    // Format string validation helper (shared for min/max)
    auto parse_fmt = [](const char* fmt, char* dest, size_t dest_sz) {
        if (!fmt || !fmt[0]) {
            strlcpy(dest, "%.0f", dest_sz);
            return;
        }
        const char* p = fmt;
        int spec_count = 0;
        bool valid = true;
        while (*p && valid) {
            if (*p == '%') {
                p++;
                if (*p == '%') { p++; continue; }
                while (*p && strchr("-+ #0", *p)) p++;
                while (*p >= '0' && *p <= '9') p++;
                if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
                if (*p == 'f' || *p == 'e' || *p == 'g' ||
                    *p == 'F' || *p == 'E' || *p == 'G') {
                    spec_count++; p++;
                } else { valid = false; }
            } else { p++; }
        }
        if (valid && spec_count <= 1) {
            strlcpy(dest, fmt, dest_sz);
        } else {
            strlcpy(dest, "%.0f", dest_sz);
            LOGW(TAG, "Invalid format '%s', using default", fmt);
        }
    };
    parse_fmt(btn["widget_sparkline_min_fmt"] | "", cfg->min_fmt, sizeof(cfg->min_fmt));
    parse_fmt(btn["widget_sparkline_max_fmt"] | "", cfg->max_fmt, sizeof(cfg->max_fmt));

    // Current-value dot
    cfg->current_dot_size = btn["widget_sparkline_current_dot"] | (uint8_t)0;

    // Reference lines (up to 3)
    for (int i = 0; i < MAX_REF_LINES; i++) {
        char key_y[40], key_c[40], key_p[40];
        snprintf(key_y, sizeof(key_y), "widget_sparkline_ref_%d_y", i + 1);
        snprintf(key_c, sizeof(key_c), "widget_sparkline_ref_%d_color", i + 1);
        snprintf(key_p, sizeof(key_p), "widget_sparkline_ref_%d_pattern", i + 1);
        JsonVariant vy = btn[key_y];
        cfg->ref_line_y[i] = vy.isNull() ? NAN : vy.as<float>();
        widget_parse_field(btn[key_c], cfg->ref_line_color[i], sizeof(cfg->ref_line_color[i]), "#888888");
        cfg->ref_line_pattern[i] = btn[key_p] | (uint8_t)0;
        if (cfg->ref_line_pattern[i] > 2) cfg->ref_line_pattern[i] = 0;
    }
    cfg->ref_in_view = btn["widget_sparkline_ref_in_view"] | false;

    // Smoothing: Gaussian kernel radius (0 = off, 1-8)
    uint8_t sm = btn["widget_sparkline_smooth"] | (uint8_t)0;
    cfg->smooth_factor = (sm > 8) ? 8 : sm;
}

// Helper: populate line color array from config (used by create and redraw)
static inline void sparkline_get_line_colors(const SparklineConfig* cfg,
                                             uint32_t out[MAX_SPARKLINE_LINES]) {
    out[0] = resolve_color(cfg->line_color, 0x4CAF50);
    out[1] = resolve_color(cfg->line_color_2, 0x2196F3);
    out[2] = resolve_color(cfg->line_color_3, 0x9C27B0);
}

static void sparkline_create(lv_obj_t* tile, const WidgetConfig* wcfg,
                              const ScreenButtonConfig* btn,
                              const PadRect* rect, const UIScaleInfo* scale,
                              lv_obj_t* icon_img, WidgetState* state) {
    auto* cfg = reinterpret_cast<const SparklineConfig*>(wcfg->data);
    auto* st = reinterpret_cast<SparklineState*>(state->data);
    memset(st, 0, sizeof(SparklineState));
    st->slot_count = cfg->slot_count;

    // Determine line count from binding presence (same pattern as gauge rings)
    st->line_count = 1;
    if (wcfg->data_binding[1][0]) st->line_count = 2;
    if (wcfg->data_binding[2][0]) st->line_count = 3;

#if HAS_MQTT
    // Look up data stream handles for each line
    for (uint8_t i = 0; i < st->line_count; i++) {
        st->ds_handles[i] = DATA_STREAM_INVALID;
        if (wcfg->data_binding[i][0]) {
            st->ds_handles[i] = data_stream_find(wcfg->data_binding[i],
                                                  cfg->window_secs, cfg->slot_count);
            if (st->ds_handles[i] == DATA_STREAM_INVALID) {
                LOGW(TAG, "No data stream for binding[%d]: %s", i, wcfg->data_binding[i]);
            }
        }
    }
#endif

    // Allocate point arrays for each line
    uint16_t max_pts = cfg->slot_count;
    for (uint8_t i = 0; i < st->line_count; i++) {
        st->points[i] = (lv_point_precise_t*)lv_malloc(
            sizeof(lv_point_precise_t) * max_pts);
        if (!st->points[i]) {
            LOGE(TAG, "Failed to allocate points array[%d] (%d pts)", i, max_pts);
            return;
        }
    }

    // Compute layout — same pattern as bar chart
    bool has_top = btn->label_top[0];
    bool has_bot = btn->label_bottom[0];
    int16_t label_h = lv_font_get_line_height(scale->font_small) + 2;
    int16_t top_h = has_top ? label_h : 0;
    int16_t bot_h = has_bot ? label_h : 0;

    int16_t icon_h = 0;
    if (icon_img) {
        lv_obj_update_layout(icon_img);
        icon_h = (int16_t)lv_obj_get_height(icon_img);
        if (icon_h <= 0) icon_h = rect->h / 4;
    }
    int16_t gap = (icon_img) ? 4 : 0;

    if (icon_img) {
        lv_obj_align(icon_img, LV_ALIGN_TOP_MID, 0, top_h);
    }

    // Reserve margin for min/max labels above and below chart
    int16_t mm_margin = 0;
    if (cfg->marker_size_min > 0 || cfg->marker_size_max > 0) {
        mm_margin = label_h + 3;  // label height + dot radius + gap
    }

    int16_t chart_top = top_h + icon_h + gap + mm_margin;
    int16_t bot_margin = bot_h + 4 + mm_margin;
    lv_obj_update_layout(tile);
    int16_t content_h = (int16_t)lv_obj_get_content_height(tile);
    int16_t content_w = (int16_t)lv_obj_get_content_width(tile);
    int16_t chart_h = content_h - chart_top - bot_margin;
    if (chart_h < 8) chart_h = 8;
    int16_t chart_w = content_w;

    LOGD(TAG, "Layout: rect=%dx%d content=%dx%d chart_top=%d chart=%dx%d slots=%d window=%ds lines=%d",
         rect->w, rect->h, content_w, content_h, chart_top, chart_w, chart_h,
         cfg->slot_count, cfg->window_secs, st->line_count);

    // Create a container for the sparkline area (clips the lines)
    lv_obj_t* chart_area = lv_obj_create(tile);
    lv_obj_set_size(chart_area, chart_w, chart_h);
    lv_obj_align(chart_area, LV_ALIGN_TOP_MID, 0, chart_top);
    lv_obj_set_style_bg_opa(chart_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart_area, 0, 0);
    lv_obj_set_style_pad_all(chart_area, 0, 0);
    lv_obj_set_style_clip_corner(chart_area, true, 0);
    lv_obj_set_scrollbar_mode(chart_area, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(chart_area, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    // Create line objects — extra lines first (drawn behind main line)
    uint32_t line_colors[MAX_SPARKLINE_LINES];
    sparkline_get_line_colors(cfg, line_colors);
    for (int8_t i = (int8_t)(st->line_count - 1); i >= 0; i--) {
        lv_obj_t* line = lv_line_create(chart_area);
        lv_obj_set_style_line_color(line, lv_color_make((line_colors[i] >> 16) & 0xFF, (line_colors[i] >> 8) & 0xFF, line_colors[i] & 0xFF), 0);
        lv_obj_set_style_line_width(line, cfg->line_width, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
        lv_obj_clear_flag(line, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_line_set_points(line, st->points[i], 0);
        st->lines[i] = line;
    }

    // Allocate per-instance ref line point pairs (avoids shared static buffer)
    st->ref_pts = (lv_point_precise_t*)lv_malloc(sizeof(lv_point_precise_t) * MAX_REF_LINES * 2);
    if (!st->ref_pts) { LOGE(TAG, "Failed to alloc ref_pts"); }

    // Create reference lines behind data (move_to_index(0) sends each behind existing children)
    for (int r = 0; r < MAX_REF_LINES; r++) {
        if (!st->ref_pts) { st->ref_line_objs[r] = nullptr; continue; }
        if (!isfinite(cfg->ref_line_y[r])) { st->ref_line_objs[r] = nullptr; continue; }
        lv_obj_t* rl = lv_line_create(chart_area);
        lv_obj_set_style_line_color(rl, resolve_lv_color(cfg->ref_line_color[r], 0x888888), 0);
        lv_obj_set_style_line_width(rl, 1, 0);
        lv_obj_set_style_line_opa(rl, LV_OPA_70, 0);
        if (cfg->ref_line_pattern[r] == REF_LINE_DOTTED) {
            lv_obj_set_style_line_dash_width(rl, 2, 0);
            lv_obj_set_style_line_dash_gap(rl, 4, 0);
        } else if (cfg->ref_line_pattern[r] == REF_LINE_DASHED) {
            lv_obj_set_style_line_dash_width(rl, 6, 0);
            lv_obj_set_style_line_dash_gap(rl, 4, 0);
        }
        lv_obj_clear_flag(rl, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_move_to_index(rl, 0);  // behind data lines
        st->ref_line_objs[r] = rl;
    }

    // Create min/max marker dots and labels (tile children, on top of chart)
    auto create_marker = [&](uint8_t dot_sz, lv_obj_t** out_dot, lv_obj_t** out_lbl) {
        if (dot_sz == 0) { *out_dot = nullptr; *out_lbl = nullptr; return; }
        lv_obj_t* dot = lv_obj_create(tile);
        lv_obj_set_size(dot, dot_sz, dot_sz);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        *out_dot = dot;

        lv_obj_t* lbl = lv_label_create(tile);
        lv_obj_set_style_text_font(lbl, scale->font_small, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_clear_flag(lbl, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lbl, "");
        *out_lbl = lbl;
    };
    create_marker(cfg->marker_size_max, &st->dot_max, &st->lbl_max);
    create_marker(cfg->marker_size_min, &st->dot_min, &st->lbl_min);

    // Current-value dots (one per line, tile children, rightmost point)
    if (cfg->current_dot_size > 0) {
        for (uint8_t i = 0; i < st->line_count; i++) {
            lv_obj_t* cd = lv_obj_create(tile);
            lv_obj_set_size(cd, cfg->current_dot_size, cfg->current_dot_size);
            lv_obj_set_style_radius(cd, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(cd, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(cd, 0, 0);
            lv_obj_set_style_pad_all(cd, 0, 0);
            lv_obj_clear_flag(cd, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            lv_obj_add_flag(cd, LV_OBJ_FLAG_HIDDEN);
            st->dot_current[i] = cd;
        }
    }
}

// ---- Smoothed line info (returned by rebuild for overlay positioning) ----

struct SmoothLineInfo {
    float    smoothed_min;     // Min value after smoothing
    float    smoothed_max;     // Max value after smoothing
    float    smoothed_last;    // Smoothed value at rightmost slot
    int16_t  last_px;          // Last point pixel X (chart-area relative)
    int16_t  last_py;          // Last point pixel Y (chart-area relative)
    int16_t  min_px;           // Min point pixel X (chart-area relative)
    int16_t  min_py;           // Min point pixel Y (chart-area relative)
    int16_t  max_px;           // Max point pixel X (chart-area relative)
    int16_t  max_py;           // Max point pixel Y (chart-area relative)
    uint8_t  min_line;         // Which line owns the min (for multi-line scans)
    uint8_t  max_line;         // Which line owns the max (for multi-line scans)
    uint8_t  valid_count;      // Number of valid rendered points
};

// ---- Gaussian kernel smoothing ----
// Smooths a contiguous float array in-place using a weighted Gaussian window.
// radius = number of neighbors on each side; sigma = radius.

static void gaussian_smooth(float* vals, uint8_t n, uint8_t radius) {
    if (radius == 0 || n < 2) return;
    float* tmp = (float*)alloca(sizeof(float) * n);
    memcpy(tmp, vals, sizeof(float) * n);
    float sigma = (float)radius;
    float inv_2s2 = -0.5f / (sigma * sigma);
    for (uint8_t i = 0; i < n; i++) {
        float sum = 0.0f, wsum = 0.0f;
        int lo = (int)i - (int)radius;
        int hi = (int)i + (int)radius;
        if (lo < 0) lo = 0;
        if (hi >= (int)n) hi = (int)n - 1;
        for (int j = lo; j <= hi; j++) {
            float d = (float)(j - (int)i);
            float w = expf(inv_2s2 * d * d);
            sum += w * tmp[j];
            wsum += w;
        }
        vals[i] = sum / wsum;
    }
}

// ---- Rebuild the LVGL point array from a data stream snapshot ----

static void sparkline_rebuild_points(const SparklineConfig* cfg,
                                     lv_point_precise_t* points,
                                     lv_obj_t* line_obj,
                                     const DataStreamSnapshot* snap,
                                     int16_t chart_w, int16_t chart_h,
                                     float shared_auto_min, float shared_auto_max,
                                     float ref_expand_min, float ref_expand_max,
                                     SmoothLineInfo* out_info) {
    // Initialize output info
    if (out_info) {
        out_info->smoothed_min = INFINITY;
        out_info->smoothed_max = -INFINITY;
        out_info->smoothed_last = NAN;
        out_info->last_px = 0;
        out_info->last_py = 0;
        out_info->min_px = 0;
        out_info->min_py = 0;
        out_info->max_px = 0;
        out_info->max_py = 0;
        out_info->valid_count = 0;
    }

    // Determine Y-axis range
    float y_min = cfg->min_val;
    float y_max = cfg->max_val;
    if (isnan(y_min)) y_min = isnan(shared_auto_min) ? snap->auto_min : shared_auto_min;
    if (isnan(y_max)) y_max = isnan(shared_auto_max) ? snap->auto_max : shared_auto_max;
    if (isnan(cfg->min_val) && isfinite(ref_expand_min) && ref_expand_min < y_min) y_min = ref_expand_min;
    if (isnan(cfg->max_val) && isfinite(ref_expand_max) && ref_expand_max > y_max) y_max = ref_expand_max;
    if (y_min >= y_max) { y_min -= 1.0f; y_max += 1.0f; }
    if (!isfinite(y_min) || !isfinite(y_max)) { y_min = 0.0f; y_max = 1.0f; }
    float y_range = y_max - y_min;

    uint8_t n = snap->slot_count;

    // Step 1: Extract valid float values and their original slot indices
    float* vals = (float*)alloca(sizeof(float) * snap->count);
    uint8_t* slot_map = (uint8_t*)alloca(sizeof(uint8_t) * snap->count);
    uint8_t valid = 0;
    for (uint8_t i = 0; i < snap->count; i++) {
        uint8_t idx = (snap->count < n)
            ? i
            : (uint8_t)((snap->head + i) % n);
        float val = snap->samples[idx];
        if (!isfinite(val)) continue;
        vals[valid] = val;
        slot_map[valid] = i;
        valid++;
    }

    // Step 2: Apply Gaussian kernel smoothing on the float values
    if (cfg->smooth_factor > 0 && valid >= 2) {
        gaussian_smooth(vals, valid, cfg->smooth_factor);
    }

    // Step 3: Convert smoothed values to pixel coordinates
    float x_step = (n > 1) ? (float)chart_w / (float)(n - 1) : 0.0f;
    uint8_t x_offset = (snap->count < n) ? (n - snap->count) : 0;

    for (uint8_t vi = 0; vi < valid; vi++) {
        float val = vals[vi];
        uint8_t si = slot_map[vi];

        float ratio = (val - y_min) / y_range;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;

        int16_t px = (int16_t)((si + x_offset) * x_step);
        int16_t py = (int16_t)((1.0f - ratio) * (chart_h - 1));
        points[vi].x = (lv_value_precise_t)px;
        points[vi].y = (lv_value_precise_t)py;

        // Track smoothed min/max and their pixel coords for overlay positioning
        if (out_info) {
            if (val < out_info->smoothed_min) {
                out_info->smoothed_min = val;
                out_info->min_px = px;
                out_info->min_py = py;
            }
            if (val > out_info->smoothed_max) {
                out_info->smoothed_max = val;
                out_info->max_px = px;
                out_info->max_py = py;
            }
            out_info->smoothed_last = val;
            out_info->last_px = px;
            out_info->last_py = py;
        }
    }

    if (out_info) out_info->valid_count = valid;

    if (line_obj) {
        lv_line_set_points(line_obj, points, valid);
    }
}

// ---- Helper: read data stream snapshots and redraw all lines ----

static void sparkline_redraw(const SparklineConfig* cfg, SparklineState* st) {
    if (!st->lines[0] || !st->points[0]) return;

#if HAS_MQTT
    // Get chart area dimensions from the main line's parent
    lv_obj_t* chart_area = lv_obj_get_parent(st->lines[0]);
    lv_obj_update_layout(chart_area);
    int16_t chart_w = (int16_t)lv_obj_get_content_width(chart_area);
    int16_t chart_h = (int16_t)lv_obj_get_content_height(chart_area);

    // Pre-scan: compute shared auto min/max across all lines when unified
    float shared_auto_min = NAN;
    float shared_auto_max = NAN;
    if (cfg->unified_autoscale && st->line_count > 1
        && (isnan(cfg->min_val) || isnan(cfg->max_val))) {
        for (uint8_t i = 0; i < st->line_count; i++) {
            if (st->ds_handles[i] == DATA_STREAM_INVALID) continue;
            DataStreamSnapshot s;
            if (!data_stream_get(st->ds_handles[i], &s)) continue;
            if (s.count == 0) continue;
            if (isnan(cfg->min_val)) {
                if (isnan(shared_auto_min) || s.auto_min < shared_auto_min)
                    shared_auto_min = s.auto_min;
            }
            if (isnan(cfg->max_val)) {
                if (isnan(shared_auto_max) || s.auto_max > shared_auto_max)
                    shared_auto_max = s.auto_max;
            }
        }
    }

    // Pre-compute ref_in_view expansion bounds
    float ref_expand_min = NAN, ref_expand_max = NAN;
    if (cfg->ref_in_view) {
        for (int r = 0; r < MAX_REF_LINES; r++) {
            float ry = cfg->ref_line_y[r];
            if (!isfinite(ry)) continue;
            if (isnan(ref_expand_min) || ry < ref_expand_min) ref_expand_min = ry;
            if (isnan(ref_expand_max) || ry > ref_expand_max) ref_expand_max = ry;
        }
    }

    // Resolve line colors once per redraw (bindings may have changed)
    uint32_t line_colors_resolved[MAX_SPARKLINE_LINES];
    sparkline_get_line_colors(cfg, line_colors_resolved);

    // Redraw each line from its data stream; collect smoothed info per line
    SmoothLineInfo smooth_info[MAX_SPARKLINE_LINES];
    memset(smooth_info, 0, sizeof(smooth_info));
    DataStreamSnapshot main_snap;
    bool main_snap_valid = false;
    for (uint8_t i = 0; i < st->line_count; i++) {
        if (!st->lines[i] || !st->points[i]) continue;

        DataStreamSnapshot snap;
        if (!data_stream_get(st->ds_handles[i], &snap)) continue;
        if (snap.count == 0) continue;

        sparkline_rebuild_points(cfg, st->points[i], st->lines[i],
                                 &snap, chart_w, chart_h,
                                 shared_auto_min, shared_auto_max,
                                 ref_expand_min, ref_expand_max,
                                 &smooth_info[i]);

        // Re-resolve line color (binding may have changed)
        if (i == 0 && cfg->use_thresholds && isfinite(snap.last_value)) {
            // Threshold coloring applies to main line only
            lv_color_t color = pick_tier_color(cfg, snap.auto_min, snap.auto_max, snap.last_value);
            lv_obj_set_style_line_color(st->lines[0], color, 0);
        } else {
            uint32_t lc = line_colors_resolved[i];
            lv_obj_set_style_line_color(st->lines[i],
                lv_color_make((lc >> 16) & 0xFF, (lc >> 8) & 0xFF, lc & 0xFF), 0);
        }
        if (i == 0) { main_snap = snap; main_snap_valid = true; }
    }

    // ---- Reference line positioning ----
    // Compute effective Y range once (shared by ref lines, min/max, current dot)
    float eff_y_min = cfg->min_val, eff_y_max = cfg->max_val;
    if (main_snap_valid) {
        if (isnan(eff_y_min)) eff_y_min = isnan(shared_auto_min) ? main_snap.auto_min : shared_auto_min;
        if (isnan(eff_y_max)) eff_y_max = isnan(shared_auto_max) ? main_snap.auto_max : shared_auto_max;
    }
    // Expand auto-scaled range to include reference lines if configured
    if (isnan(cfg->min_val) && isfinite(ref_expand_min) && ref_expand_min < eff_y_min) eff_y_min = ref_expand_min;
    if (isnan(cfg->max_val) && isfinite(ref_expand_max) && ref_expand_max > eff_y_max) eff_y_max = ref_expand_max;
    if (eff_y_min >= eff_y_max) { eff_y_min -= 1.0f; eff_y_max += 1.0f; }
    if (!isfinite(eff_y_min) || !isfinite(eff_y_max)) { eff_y_min = 0.0f; eff_y_max = 1.0f; }
    float eff_y_range = eff_y_max - eff_y_min;

    // Helper lambdas for coordinate mapping
    auto val_to_y = [&](float val) -> int16_t {
        float ratio = (val - eff_y_min) / eff_y_range;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        return (int16_t)((1.0f - ratio) * (chart_h - 1));
    };

    // Use per-instance point pairs for reference lines (2 points each: left to right)
    for (int r = 0; r < MAX_REF_LINES; r++) {
        if (!st->ref_line_objs[r]) continue;
        float ry_val = cfg->ref_line_y[r];
        if (!isfinite(ry_val) || ry_val < eff_y_min || ry_val > eff_y_max) {
            // Outside visible range or disabled — hide
            lv_line_set_points(st->ref_line_objs[r], &st->ref_pts[r * 2], 0);
            lv_obj_add_flag(st->ref_line_objs[r], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int16_t ry = val_to_y(ry_val);
        st->ref_pts[r * 2 + 0] = { 0, (lv_value_precise_t)ry };
        st->ref_pts[r * 2 + 1] = { (lv_value_precise_t)chart_w, (lv_value_precise_t)ry };
        lv_line_set_points(st->ref_line_objs[r], &st->ref_pts[r * 2], 2);
        lv_obj_set_style_line_color(st->ref_line_objs[r], resolve_lv_color(cfg->ref_line_color[r], 0x888888), 0);
        lv_obj_clear_flag(st->ref_line_objs[r], LV_OBJ_FLAG_HIDDEN);
    }

    // ---- Chart area position for tile-child overlays ----
    int16_t ca_x = (int16_t)lv_obj_get_x(chart_area);
    int16_t ca_y = (int16_t)lv_obj_get_y(chart_area);
    lv_obj_t* tile = lv_obj_get_parent(chart_area);
    int16_t tile_cw = (int16_t)lv_obj_get_content_width(tile);

    // ---- Current-value dot positioning (one per line, uses actual line endpoint) ----
    for (uint8_t ci = 0; ci < st->line_count; ci++) {
        if (!st->dot_current[ci]) continue;
        const SmoothLineInfo& si = smooth_info[ci];
        if (si.valid_count > 0 && isfinite(si.smoothed_last)) {
            int16_t cr = cfg->current_dot_size / 2;

            // Color: threshold color if thresholds on (main line only), else line color
            lv_color_t cd_clr;
            if (ci == 0 && cfg->use_thresholds) {
                cd_clr = pick_tier_color(cfg,
                    main_snap_valid ? main_snap.auto_min : si.smoothed_min,
                    main_snap_valid ? main_snap.auto_max : si.smoothed_max,
                    si.smoothed_last);
            } else {
                uint32_t lc = line_colors_resolved[ci];
                cd_clr = lv_color_make((lc >> 16) & 0xFF, (lc >> 8) & 0xFF, lc & 0xFF);
            }
            lv_obj_set_style_bg_color(st->dot_current[ci], cd_clr, 0);
            lv_obj_set_pos(st->dot_current[ci], ca_x + si.last_px - cr, ca_y + si.last_py - cr);
            lv_obj_clear_flag(st->dot_current[ci], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(st->dot_current[ci], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // ---- Min/max marker positioning ----
    bool has_min_marker = (st->dot_min != nullptr);
    bool has_max_marker = (st->dot_max != nullptr);
    if (has_min_marker || has_max_marker) {
        // Determine scan scope: shared scale → all lines, else main only
        bool use_shared = cfg->unified_autoscale && st->line_count > 1;
        uint8_t scan_count = (use_shared || st->line_count == 1) ? st->line_count : 1;

        float found_min = INFINITY, found_max = -INFINITY;
        int16_t min_px = 0, min_py = 0, max_px = 0, max_py = 0;
        uint8_t min_line = 0, max_line = 0;

        for (uint8_t li = 0; li < scan_count; li++) {
            const SmoothLineInfo& si = smooth_info[li];
            if (si.valid_count == 0) continue;
            if (si.smoothed_min < found_min) {
                found_min = si.smoothed_min;
                min_px = si.min_px;
                min_py = si.min_py;
                min_line = li;
            }
            if (si.smoothed_max > found_max) {
                found_max = si.smoothed_max;
                max_px = si.max_px;
                max_py = si.max_py;
                max_line = li;
            }
        }

        if (isfinite(found_min) && isfinite(found_max)) {
            // Resolve marker label color: use override if set, else line color
            auto resolve_marker_color = [&](const char* override_str, uint8_t line_idx) -> lv_color_t {
                uint32_t c = override_str[0] ? resolve_color(override_str, line_colors_resolved[line_idx]) : line_colors_resolved[line_idx];
                return lv_color_make((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
            };

            char buf[24];

            // Position max dot + label (above)
            if (has_max_marker) {
                int16_t max_cx = max_px;
                int16_t max_cy = max_py;
                int16_t max_r = cfg->marker_size_max / 2;
                lv_color_t max_clr = resolve_marker_color(cfg->max_label_color, max_line);
                lv_obj_set_style_bg_color(st->dot_max, max_clr, 0);
                lv_obj_set_pos(st->dot_max, ca_x + max_cx - max_r, ca_y + max_cy - max_r);
                lv_obj_clear_flag(st->dot_max, LV_OBJ_FLAG_HIDDEN);

                snprintf(buf, sizeof(buf), cfg->max_fmt, (double)found_max);
                lv_label_set_text(st->lbl_max, buf);
                lv_obj_set_style_text_color(st->lbl_max, max_clr, 0);
                lv_obj_update_layout(st->lbl_max);
                int16_t lw = (int16_t)lv_obj_get_width(st->lbl_max);
                int16_t lh = (int16_t)lv_obj_get_height(st->lbl_max);
                int16_t lx = ca_x + max_cx - lw / 2;
                if (lx < 0) lx = 0;
                if (lx + lw > tile_cw) lx = tile_cw - lw;
                lv_obj_set_pos(st->lbl_max, lx, ca_y + max_cy - max_r - lh - 1);
                lv_obj_clear_flag(st->lbl_max, LV_OBJ_FLAG_HIDDEN);
            }

            // Position min dot + label (below)
            if (has_min_marker) {
                int16_t min_cx = min_px;
                int16_t min_cy = min_py;
                int16_t min_r = cfg->marker_size_min / 2;
                lv_color_t min_clr = resolve_marker_color(cfg->min_label_color, min_line);
                lv_obj_set_style_bg_color(st->dot_min, min_clr, 0);
                lv_obj_set_pos(st->dot_min, ca_x + min_cx - min_r, ca_y + min_cy - min_r);
                lv_obj_clear_flag(st->dot_min, LV_OBJ_FLAG_HIDDEN);

                snprintf(buf, sizeof(buf), cfg->min_fmt, (double)found_min);
                lv_label_set_text(st->lbl_min, buf);
                lv_obj_set_style_text_color(st->lbl_min, min_clr, 0);
                lv_obj_update_layout(st->lbl_min);
                int16_t lw = (int16_t)lv_obj_get_width(st->lbl_min);
                int16_t lx = ca_x + min_cx - lw / 2;
                if (lx < 0) lx = 0;
                if (lx + lw > tile_cw) lx = tile_cw - lw;
                lv_obj_set_pos(st->lbl_min, lx, ca_y + min_cy + min_r + 1);
                lv_obj_clear_flag(st->lbl_min, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            if (st->dot_min) lv_obj_add_flag(st->dot_min, LV_OBJ_FLAG_HIDDEN);
            if (st->dot_max) lv_obj_add_flag(st->dot_max, LV_OBJ_FLAG_HIDDEN);
            if (st->lbl_min) lv_obj_add_flag(st->lbl_min, LV_OBJ_FLAG_HIDDEN);
            if (st->lbl_max) lv_obj_add_flag(st->lbl_max, LV_OBJ_FLAG_HIDDEN);
        }
    }
#endif
}

static void sparkline_update(lv_obj_t* tile, const WidgetConfig* wcfg,
                              WidgetState* state, const char* raw_value) {
    auto* cfg = reinterpret_cast<const SparklineConfig*>(wcfg->data);
    auto* st = reinterpret_cast<SparklineState*>(state->data);
    if (!st->lines[0] || !st->points[0]) return;

    // Data ingestion is handled by data_stream_poll() — we just redraw.
    sparkline_redraw(cfg, st);
}

// ---- Tick: redraw from registry even when no new data value ----

static void sparkline_tick(lv_obj_t* tile, const WidgetConfig* wcfg,
                           WidgetState* state) {
    auto* cfg = reinterpret_cast<const SparklineConfig*>(wcfg->data);
    auto* st = reinterpret_cast<SparklineState*>(state->data);
    if (!st->lines[0] || !st->points[0]) return;

    // Redraw from the data stream (which advances time independently)
    sparkline_redraw(cfg, st);
}

static void sparkline_destroy(WidgetState* state) {
    auto* st = reinterpret_cast<SparklineState*>(state->data);
    for (uint8_t i = 0; i < MAX_SPARKLINE_LINES; i++) {
        if (st->points[i]) { lv_free(st->points[i]); st->points[i] = nullptr; }
    }
    if (st->ref_pts) { lv_free(st->ref_pts); st->ref_pts = nullptr; }
    // LVGL line + chart_area are tile children — deleted automatically
    // Ring buffers are owned by data_stream registry — not freed here
}

// ---- Registration ----

static bool sparkline_get_stream_params(const WidgetConfig* wcfg,
                                        uint8_t stream_index,
                                        uint16_t* window_secs,
                                        uint8_t* slot_count,
                                        const char** out_binding) {
    auto* cfg = reinterpret_cast<const SparklineConfig*>(wcfg->data);
    if (stream_index >= MAX_WIDGET_BINDINGS) return false;
    const char* binding = wcfg->data_binding[stream_index];
    if (!binding || !binding[0]) return false;
    if (window_secs) *window_secs = cfg->window_secs;
    if (slot_count)  *slot_count  = cfg->slot_count;
    if (out_binding) *out_binding = binding;
    return true;
}

REGISTER_WIDGET(sparkline, sparkline_get_stream_params);

#endif // HAS_DISPLAY
