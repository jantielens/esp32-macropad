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

struct SparklineConfig {
    float    min_val;              // Y-axis minimum (NAN = auto-scale)
    float    max_val;              // Y-axis maximum (NAN = auto-scale)
    float    threshold_1;          // Color tier breakpoints
    float    threshold_2;
    float    threshold_3;
    uint32_t color_good_rgb;       // Color tier 0: below threshold 1
    uint32_t color_ok_rgb;         // Color tier 1: threshold 1 → 2
    uint32_t color_attention_rgb;  // Color tier 2: threshold 2 → 3
    uint32_t color_warning_rgb;    // Color tier 3: at/above threshold 3
    uint32_t line_color_rgb;       // Fallback line color (when thresholds not used)
    uint16_t window_secs;          // Time window in seconds (e.g. 300 = 5 min)
    uint8_t  slot_count;           // Number of sample slots (default 60)
    uint8_t  line_width;           // Line thickness in pixels (default 2)
    bool     use_absolute;         // Compare |value| for color tiers
    bool     use_thresholds;       // true = color by current value tier
};

static_assert(sizeof(SparklineConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "SparklineConfig exceeds WIDGET_CONFIG_MAX_BYTES");

// ---- Runtime state (packed into WidgetState.data[]) ----
// The sparkline no longer owns its ring buffer — data collection is
// handled by the DataStream registry (data_stream.cpp) which runs
// independently of the active screen.  SparklineState just holds the
// LVGL rendering objects and a handle to its data stream.

struct SparklineState {
    lv_obj_t*            line;       // LVGL line object
    lv_point_precise_t*  points;     // Heap-allocated LVGL point array [slot_count]
    uint8_t  slot_count;             // Copy from config for points array size
#if HAS_MQTT
    data_stream_handle_t ds_handle;  // Handle into data stream registry
#endif
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

    uint32_t rgb;
    if (cmp >= t3) rgb = cfg->color_warning_rgb;
    else if (cmp >= t2) rgb = cfg->color_attention_rgb;
    else if (cmp >= t1) rgb = cfg->color_ok_rgb;
    else rgb = cfg->color_good_rgb;
    return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
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

    cfg->line_color_rgb = widget_parse_color(btn["widget_sparkline_line_color"], 0x4CAF50);
    cfg->use_thresholds = btn["widget_sparkline_use_thresholds"] | false;
    cfg->use_absolute = btn["widget_use_absolute"] | false;

    // Color tiers (same defaults as bar chart)
    cfg->color_good_rgb     = widget_parse_color(btn["widget_color_good"],      0x4CAF50);
    cfg->color_ok_rgb       = widget_parse_color(btn["widget_color_ok"],        0x8BC34A);
    cfg->color_attention_rgb = widget_parse_color(btn["widget_color_attention"], 0xFF9800);
    cfg->color_warning_rgb  = widget_parse_color(btn["widget_color_warning"],   0xF44336);

    // Thresholds: NAN = auto (computed dynamically from effective range)
    JsonVariant vt1 = btn["widget_threshold_1"];
    JsonVariant vt2 = btn["widget_threshold_2"];
    JsonVariant vt3 = btn["widget_threshold_3"];
    cfg->threshold_1 = vt1.isNull() ? NAN : vt1.as<float>();
    cfg->threshold_2 = vt2.isNull() ? NAN : vt2.as<float>();
    cfg->threshold_3 = vt3.isNull() ? NAN : vt3.as<float>();
}

static void sparkline_create(lv_obj_t* tile, const WidgetConfig* wcfg,
                              const ScreenButtonConfig* btn,
                              const PadRect* rect, const UIScaleInfo* scale,
                              lv_obj_t* icon_img, WidgetState* state) {
    auto* cfg = reinterpret_cast<const SparklineConfig*>(wcfg->data);
    auto* st = reinterpret_cast<SparklineState*>(state->data);
    memset(st, 0, sizeof(SparklineState));
    st->slot_count = cfg->slot_count;
#if HAS_MQTT
    st->ds_handle = data_stream_find(wcfg->data_binding,
                                     cfg->window_secs, cfg->slot_count);
    if (st->ds_handle == DATA_STREAM_INVALID) {
        LOGW(TAG, "No data stream for binding: %s", wcfg->data_binding);
    }
#endif

    // Allocate point array via lv_malloc (PSRAM-first)
    st->points = (lv_point_precise_t*)lv_malloc(sizeof(lv_point_precise_t) * cfg->slot_count);
    if (!st->points) {
        LOGE(TAG, "Failed to allocate points array (%d slots)", cfg->slot_count);
        return;
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

    int16_t chart_top = top_h + icon_h + gap;
    int16_t bot_margin = bot_h + 4;
    lv_obj_update_layout(tile);
    int16_t content_h = (int16_t)lv_obj_get_content_height(tile);
    int16_t content_w = (int16_t)lv_obj_get_content_width(tile);
    int16_t chart_h = content_h - chart_top - bot_margin;
    if (chart_h < 8) chart_h = 8;
    int16_t chart_w = content_w;

    LOGD(TAG, "Layout: rect=%dx%d content=%dx%d chart_top=%d chart=%dx%d slots=%d window=%ds",
         rect->w, rect->h, content_w, content_h, chart_top, chart_w, chart_h, cfg->slot_count, cfg->window_secs);

    // Create a container for the sparkline area (clips the line)
    lv_obj_t* chart_area = lv_obj_create(tile);
    lv_obj_set_size(chart_area, chart_w, chart_h);
    lv_obj_align(chart_area, LV_ALIGN_TOP_MID, 0, chart_top);
    lv_obj_set_style_bg_opa(chart_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart_area, 0, 0);
    lv_obj_set_style_pad_all(chart_area, 0, 0);
    lv_obj_set_style_clip_corner(chart_area, true, 0);
    lv_obj_set_scrollbar_mode(chart_area, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(chart_area, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    // Create line object inside the chart area
    lv_obj_t* line = lv_line_create(chart_area);
    uint32_t lc = cfg->line_color_rgb;
    lv_obj_set_style_line_color(line, lv_color_make((lc >> 16) & 0xFF, (lc >> 8) & 0xFF, lc & 0xFF), 0);
    lv_obj_set_style_line_width(line, cfg->line_width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_clear_flag(line, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    // Initialize with zero points (nothing drawn until data arrives)
    lv_line_set_points(line, st->points, 0);

    st->line = line;
}

// ---- Rebuild the LVGL point array from a data stream snapshot ----

static void sparkline_rebuild_points(const SparklineConfig* cfg,
                                     SparklineState* st,
                                     const DataStreamSnapshot* snap,
                                     int16_t chart_w, int16_t chart_h) {
    // Determine Y-axis range
    float y_min = cfg->min_val;
    float y_max = cfg->max_val;
    if (isnan(y_min)) y_min = snap->auto_min;
    if (isnan(y_max)) y_max = snap->auto_max;
    if (y_min >= y_max) { y_min -= 1.0f; y_max += 1.0f; }  // degenerate range guard
    if (!isfinite(y_min) || !isfinite(y_max)) { y_min = 0.0f; y_max = 1.0f; }
    float y_range = y_max - y_min;

    // Build points from oldest to newest
    uint8_t n = snap->slot_count;
    uint8_t valid = 0;
    float x_step = (n > 1) ? (float)chart_w / (float)(n - 1) : 0.0f;

    // Walk the ring buffer from oldest to newest
    for (uint8_t i = 0; i < n; i++) {
        uint8_t idx = (snap->count < n)
            ? i                                    // buffer not yet full: 0..count-1
            : (uint8_t)((snap->head + i) % n);    // buffer full: head is oldest
        if (i >= snap->count) break;               // no more valid samples
        float val = snap->samples[idx];
        if (!isfinite(val)) continue;              // skip invalid entries

        float ratio = (val - y_min) / y_range;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;

        // X: left-to-right (oldest=left, newest=right)
        st->points[valid].x = (lv_value_precise_t)(i * x_step);
        // Y: top=max, bottom=min (LVGL Y grows downward)
        st->points[valid].y = (lv_value_precise_t)((1.0f - ratio) * (chart_h - 1));
        valid++;
    }

    // Update the line with the valid point count
    if (st->line) {
        lv_line_set_points(st->line, st->points, valid);
    }
}

// ---- Helper: read data stream snapshot and redraw ----

static void sparkline_redraw(const SparklineConfig* cfg, SparklineState* st) {
    if (!st->line || !st->points) return;

#if HAS_MQTT
    DataStreamSnapshot snap;
    if (!data_stream_get(st->ds_handle, &snap)) return;
    if (snap.count == 0) return;

    lv_obj_t* chart_area = lv_obj_get_parent(st->line);
    lv_obj_update_layout(chart_area);
    int16_t chart_w = (int16_t)lv_obj_get_content_width(chart_area);
    int16_t chart_h = (int16_t)lv_obj_get_content_height(chart_area);

    sparkline_rebuild_points(cfg, st, &snap, chart_w, chart_h);

    // Update line color based on current value (threshold mode)
    if (cfg->use_thresholds && isfinite(snap.last_value)) {
        lv_color_t color = pick_tier_color(cfg, snap.auto_min, snap.auto_max, snap.last_value);
        lv_obj_set_style_line_color(st->line, color, 0);
    }
#endif
}

static void sparkline_update(lv_obj_t* tile, const WidgetConfig* wcfg,
                              WidgetState* state, const char* raw_value) {
    auto* cfg = reinterpret_cast<const SparklineConfig*>(wcfg->data);
    auto* st = reinterpret_cast<SparklineState*>(state->data);
    if (!st->line || !st->points) return;

    // Data ingestion is handled by data_stream_poll() — we just redraw.
    sparkline_redraw(cfg, st);
}

// ---- Tick: redraw from registry even when no new data value ----

static void sparkline_tick(lv_obj_t* tile, const WidgetConfig* wcfg,
                           WidgetState* state) {
    auto* cfg = reinterpret_cast<const SparklineConfig*>(wcfg->data);
    auto* st = reinterpret_cast<SparklineState*>(state->data);
    if (!st->line || !st->points) return;

    // Redraw from the data stream (which advances time independently)
    sparkline_redraw(cfg, st);
}

static void sparkline_destroy(WidgetState* state) {
    auto* st = reinterpret_cast<SparklineState*>(state->data);
    if (st->points) { lv_free(st->points); st->points = nullptr; }
    // LVGL line + chart_area are tile children — deleted automatically
    // Ring buffer is owned by data_stream registry — not freed here
}

// ---- Registration ----

static bool sparkline_get_stream_params(const uint8_t* config_data,
                                        uint16_t* window_secs,
                                        uint8_t* slot_count) {
    auto* cfg = reinterpret_cast<const SparklineConfig*>(config_data);
    if (window_secs) *window_secs = cfg->window_secs;
    if (slot_count)  *slot_count  = cfg->slot_count;
    return true;
}

const WidgetType sparkline_widget_type = {
    "sparkline",
    sparkline_parse,
    sparkline_create,
    sparkline_update,
    sparkline_destroy,
    sparkline_tick,
    sparkline_get_stream_params
};

#endif // HAS_DISPLAY
