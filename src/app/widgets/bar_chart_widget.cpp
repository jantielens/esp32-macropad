#include "widget.h"

#if HAS_DISPLAY

#include "../binding_template.h"
#include "../log_manager.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TAG "BarChart"

// ============================================================================
// Bar Chart Widget
// ============================================================================
// Renders a vertical bar inside the button tile. The bar height and color
// are driven by an MQTT value (via the existing label binding system).
//
// Layout within the tile:
//   ┌─────────────────────┐
//   │   label_top (title)  │
//   │        icon           │
//   │  ┌───────────────┐   │
//   │  │  BAR FILL     │   │  ← grows from bottom
//   │  │               │   │
//   │  └───────────────┘   │
//   │   label_bottom (val) │
//   └─────────────────────┘

// ---- Config struct (packed into WidgetConfig.data[]) ----

struct BarChartConfig {
    char     bar_min[CONFIG_BINDABLE_SHORT_LEN];        // Minimum value (bindable, default "0")
    char     bar_max[CONFIG_BINDABLE_SHORT_LEN];        // Full-scale value (bindable, default "3")
    char     bar_color[CONFIG_COLOR_MAX_LEN];            // Bar fill color (bindable, default "#4CAF50")
    char     bar_bg_color[CONFIG_BINDABLE_SHORT_LEN];    // Bar track background (default "#1A1A1A")
    uint8_t  bar_width_pct;       // Bar width as % of tile width (1-100, default 100)
    bool     horizontal;          // true = horizontal bar (grows left→right)
    bool     zero_centered;       // Fill from the zero baseline instead of min (default false)
    uint16_t anim_ms;             // Transition duration in ms (0 = instant, default 300)
    // Scale tick gridlines
    uint8_t  tick_count;          // Gridline count (0 = none, default 0)
    char     tick_color[CONFIG_BINDABLE_SHORT_LEN];     // Gridline color (bindable, default "#808080")
    uint8_t  tick_width;          // Gridline width in pixels (1–5, default 1)
    // Target marker (disabled when marker_value is empty)
    char     marker_value[CONFIG_BINDABLE_SHORT_LEN];   // Target position on scale (bindable, "" = disabled)
    char     marker_color[CONFIG_BINDABLE_SHORT_LEN];   // Marker line color (bindable, default "#FFFFFF")
    char     marker_zone_color[CONFIG_BINDABLE_SHORT_LEN]; // Zone band color (bindable, default "#FF5722")
    uint8_t  marker_width;        // Marker line width in pixels (0 = no line, default 2)
    uint8_t  marker_zone_pct;     // Zone band total size as % of range (0 = no zone, max 100)
};

static_assert(sizeof(BarChartConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "BarChartConfig exceeds WIDGET_CONFIG_MAX_BYTES");

// ---- Runtime state (packed into WidgetState.data[]) ----

struct BarChartState {
    lv_obj_t* bar_bg;    // Bar background rectangle
    lv_obj_t* bar_fill;  // Bar fill rectangle (grows from bottom)
    lv_obj_t* marker_line; // Target marker line (or nullptr)
    lv_obj_t* marker_zone; // Target zone band overlay (or nullptr)
    lv_obj_t** tick_lines; // Heap-allocated gridline pointers (or nullptr)
    float     last_value; // Last numeric value (for skipping redundant updates)
    float     cached_min; // Last resolved min (for detecting binding changes in tick)
    float     cached_max; // Last resolved max (for detecting binding changes in tick)
    float     cached_marker_val;    // Last resolved marker position
    uint32_t  cached_bar_bg_color;  // Last resolved bar background color
    uint32_t  cached_bar_color;     // Last resolved bar fill color
    uint32_t  cached_tick_color;    // Last resolved gridline color
    uint32_t  cached_marker_color;  // Last resolved marker line color
    uint32_t  cached_marker_zone_color; // Last resolved zone band color
    uint32_t  last_update_ms;       // Timestamp of last update (rapid-change detection)
    int16_t   zero_px;              // Zero baseline position in px (zero-centered mode)
    int16_t   last_anim_px;         // Last animated quantity (fill px or value-edge px)
    uint8_t   tick_line_count;      // Number of cached gridline pointers
    bool      has_received_data;    // True after first value received (snap on first)
    bool      horizontal;           // Cached orientation for anim callback
    bool      zero_centered;        // Cached zero-centered mode for anim callback
};

static_assert(sizeof(BarChartState) <= WIDGET_STATE_MAX_BYTES,
              "BarChartState exceeds WIDGET_STATE_MAX_BYTES");

// ---- WidgetType callbacks ----

static void bar_chart_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<BarChartConfig*>(data);
    memset(cfg, 0, sizeof(BarChartConfig));

    widget_parse_field(btn["widget_bar_min"],      cfg->bar_min,       sizeof(cfg->bar_min),       "0", false);
    widget_parse_field(btn["widget_bar_max"],      cfg->bar_max,       sizeof(cfg->bar_max),       "3", false);

    widget_parse_field(btn["widget_bar_color"],     cfg->bar_color,     sizeof(cfg->bar_color),     "#4CAF50");
    widget_parse_field(btn["widget_bar_bg_color"],  cfg->bar_bg_color,  sizeof(cfg->bar_bg_color),  "#1A1A1A");
    uint8_t wpct = btn["widget_bar_width_pct"] | (uint8_t)100;
    cfg->bar_width_pct = clamp_val<uint8_t>(wpct, 1, 100);

    const char* orient = btn["widget_orientation"] | "";
    cfg->horizontal = (orient[0] == 'h' || orient[0] == 'H');

    cfg->zero_centered = btn["widget_bar_zero_centered"] | false;

    int ams = btn["widget_anim_ms"] | 300;
    cfg->anim_ms = (uint16_t)clamp_val(ams, 0, 5000);

    // Scale tick gridlines
    uint8_t tc = btn["widget_bar_ticks"] | (uint8_t)0;
    cfg->tick_count = (tc > 20) ? 20 : tc;
    widget_parse_field(btn["widget_bar_tick_color"], cfg->tick_color, sizeof(cfg->tick_color), "#808080");
    uint8_t tw = btn["widget_bar_tick_width"] | (uint8_t)1;
    cfg->tick_width = clamp_val<uint8_t>(tw, 1, 5);

    // Target marker (disabled when marker_value is empty)
    widget_parse_field(btn["widget_bar_marker_value"], cfg->marker_value, sizeof(cfg->marker_value), "", false);
    widget_parse_field(btn["widget_bar_marker_color"], cfg->marker_color, sizeof(cfg->marker_color), "#FFFFFF");
    widget_parse_field(btn["widget_bar_marker_zone_color"], cfg->marker_zone_color, sizeof(cfg->marker_zone_color), "#FF5722");
    uint8_t mw = btn["widget_bar_marker_width"] | (uint8_t)2;
    cfg->marker_width = (mw > 10) ? 10 : mw;
    uint8_t mzp = btn["widget_bar_marker_zone_pct"] | (uint8_t)0;
    cfg->marker_zone_pct = (mzp > 100) ? 100 : mzp;
}

static void bar_chart_create(lv_obj_t* tile, const WidgetConfig* wcfg,
                              const ScreenButtonConfig* btn,
                              const PadRect* rect, const UIScaleInfo* scale,
                              lv_obj_t* icon_img, lv_obj_t* center_label,
                              WidgetState* state) {
    auto* cfg = reinterpret_cast<const BarChartConfig*>(wcfg->data);
    auto* st = reinterpret_cast<BarChartState*>(state->data);
    memset(st, 0, sizeof(BarChartState));
    st->last_value = NAN;
    st->cached_min = NAN;
    st->cached_max = NAN;
    st->cached_marker_val = NAN;
    st->cached_bar_bg_color = COLOR_CACHE_INIT;
    st->cached_bar_color    = COLOR_CACHE_INIT;
    st->cached_tick_color   = COLOR_CACHE_INIT;
    st->cached_marker_color = COLOR_CACHE_INIT;
    st->cached_marker_zone_color = COLOR_CACHE_INIT;

    // Only reserve space for labels that are actually used (static text or MQTT binding)
    bool has_top = btn->label_top[0];
    bool has_bot = btn->label_bottom[0];
    int16_t label_h = lv_font_get_line_height(scale->font_small) + 2;
    int16_t top_h = has_top ? label_h : 0;
    int16_t bot_h = has_bot ? label_h : 0;
    const int16_t ui_ofs_x = btn->ui_offset_x;
    const int16_t ui_ofs_y = btn->ui_offset_y;
    // Use actual icon image height if available (PNG is pre-sized by JS)
    int16_t icon_h = 0;
    if (icon_img) {
        // Force layout so lv_obj_get_height returns the real image size
        lv_obj_update_layout(icon_img);
        icon_h = (int16_t)lv_obj_get_height(icon_img);
        if (icon_h <= 0) icon_h = rect->h / 4;  // fallback: quarter tile
    }
    int16_t center_label_h = 0;
    if (center_label) {
        lv_obj_update_layout(center_label);
        center_label_h = (int16_t)lv_obj_get_height(center_label);
    }
    int16_t header_h = icon_h;
    if (center_label_h > 0) {
        if (icon_h > 0) header_h += 4 + center_label_h;  // icon + gap + label
        else             header_h = center_label_h;
    }
    int16_t gap = 6;  // margin between header and bar

    // Position icon right below the top label
    if (icon_img) {
        lv_obj_align(icon_img, LV_ALIGN_TOP_MID, ui_ofs_x, top_h + ui_ofs_y);
    }
    if (center_label && !icon_img) {
        lv_obj_align(center_label, LV_ALIGN_TOP_MID, ui_ofs_x + btn->style_center.x_offset,
                     top_h + btn->style_center.y_offset + ui_ofs_y);
    } else if (center_label && icon_img) {
        lv_obj_align(center_label, LV_ALIGN_TOP_MID, ui_ofs_x + btn->style_center.x_offset,
                     top_h + icon_h + 4 + btn->style_center.y_offset + ui_ofs_y);
    }

    // Ask LVGL for the actual content dimensions (accounts for padding + border)
    lv_obj_update_layout(tile);
    int16_t content_h = (int16_t)lv_obj_get_content_height(tile);
    int16_t content_w = (int16_t)lv_obj_get_content_width(tile);

    int16_t bar_w, bar_h, bar_top;
    int16_t bar_bottom_margin;

    if (cfg->horizontal) {
        // Horizontal: bar spans the full width, height controlled by bar_width_pct
        int16_t bar_top_start = top_h + header_h + (header_h > 0 ? gap : 0);
        bar_bottom_margin = bot_h + 4;
        int16_t avail_h = content_h - bar_top_start - bar_bottom_margin;
        if (avail_h < 8) avail_h = 8;
        bar_h = (int16_t)(avail_h * cfg->bar_width_pct / 100);
        if (bar_h < 4) bar_h = 4;
        bar_w = content_w;  // full content width
        bar_top = bar_top_start + (avail_h - bar_h) / 2; // vertically center in available space
    } else {
        // Vertical (original): bar spans the full height, width controlled by bar_width_pct
        bar_top = top_h + header_h + gap;
        bar_bottom_margin = bot_h + 4;
        bar_h = content_h - bar_top - bar_bottom_margin;
        if (bar_h < 8) bar_h = 8;
        int16_t max_bar_w = rect->w - 16; // 8px margin each side
        bar_w = (int16_t)(max_bar_w * cfg->bar_width_pct / 100);
        if (bar_w < 4) bar_w = 4;
    }

    bar_top += ui_ofs_y;

    LOGD(TAG, "Layout: rect=%dx%d content_h=%d top_h=%d icon_h=%d gap=%d bar_top=%d bar_w=%d bar_h=%d bot_h=%d horiz=%d",
         rect->w, rect->h, content_h, top_h, icon_h, gap, bar_top, bar_w, bar_h, bot_h, cfg->horizontal);

    // Bar background — positioned from top so gap is guaranteed
    lv_obj_t* bar_bg = lv_obj_create(tile);
    lv_obj_set_size(bar_bg, bar_w, bar_h);
    lv_obj_align(bar_bg, LV_ALIGN_TOP_MID, ui_ofs_x, bar_top);
    lv_obj_set_style_bg_color(bar_bg, resolve_lv_color(cfg->bar_bg_color, 0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(bar_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar_bg, 4, 0);
    lv_obj_set_style_border_width(bar_bg, 0, 0);
    lv_obj_set_style_pad_all(bar_bg, 0, 0);
    lv_obj_clear_flag(bar_bg, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    // Bar fill (grows from bottom for vertical, from left for horizontal)
    lv_obj_t* bar_fill = lv_obj_create(bar_bg);
    if (cfg->horizontal) {
        lv_obj_set_size(bar_fill, 0, bar_h); // start at 0 width
        lv_obj_align(bar_fill, LV_ALIGN_LEFT_MID, 0, 0);
    } else {
        lv_obj_set_size(bar_fill, bar_w, 0); // start at 0 height
        lv_obj_align(bar_fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    lv_obj_set_style_bg_color(bar_fill, lv_color_make(0x4C, 0xAF, 0x50), 0); // default green
    lv_obj_set_style_bg_opa(bar_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar_fill, 4, 0);
    lv_obj_set_style_border_width(bar_fill, 0, 0);
    lv_obj_set_style_pad_all(bar_fill, 0, 0);
    lv_obj_clear_flag(bar_fill, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    st->bar_bg = bar_bg;
    st->bar_fill = bar_fill;
    st->has_received_data = false;
    st->horizontal = cfg->horizontal;
    st->zero_centered = cfg->zero_centered;

    // Track dimensions for gridline/marker placement (fill direction + cross axis)
    const int16_t track_total = cfg->horizontal ? bar_w : bar_h;
    const int16_t track_cross = cfg->horizontal ? bar_h : bar_w;

    // ---- Scale tick gridlines (evenly spaced, drawn over the fill) ----
    if (cfg->tick_count > 0) {
        st->tick_lines = (lv_obj_t**)lv_malloc(sizeof(lv_obj_t*) * cfg->tick_count);
        if (!st->tick_lines) {
            LOGW(TAG, "Failed to alloc tick_lines (%u)", cfg->tick_count);
        } else {
            lv_color_t tclr = resolve_lv_color(cfg->tick_color, 0x808080);
            for (uint8_t i = 1; i <= cfg->tick_count; i++) {
                float ratio = (float)i / (cfg->tick_count + 1);
                int16_t px = (int16_t)roundf(ratio * track_total);
                lv_obj_t* gl = lv_obj_create(bar_bg);
                lv_obj_set_style_bg_color(gl, tclr, 0);
                lv_obj_set_style_bg_opa(gl, LV_OPA_COVER, 0);
                lv_obj_set_style_radius(gl, 0, 0);
                lv_obj_set_style_border_width(gl, 0, 0);
                lv_obj_set_style_pad_all(gl, 0, 0);
                lv_obj_clear_flag(gl, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
                if (cfg->horizontal) {
                    lv_obj_set_size(gl, cfg->tick_width, track_cross);
                    lv_obj_align(gl, LV_ALIGN_LEFT_MID, px - cfg->tick_width / 2, 0);
                } else {
                    lv_obj_set_size(gl, track_cross, cfg->tick_width);
                    lv_obj_align(gl, LV_ALIGN_BOTTOM_MID, 0, cfg->tick_width / 2 - px);
                }
                st->tick_lines[st->tick_line_count++] = gl;
            }
        }
    }

    // ---- Target marker zone band + line (positioned in tick) ----
    if (cfg->marker_value[0]) {
        if (cfg->marker_zone_pct > 0) {
            lv_obj_t* zone = lv_obj_create(bar_bg);
            lv_obj_set_size(zone, 0, 0);
            lv_obj_set_style_bg_color(zone, resolve_lv_color(cfg->marker_zone_color, 0xFF5722), 0);
            lv_obj_set_style_bg_opa(zone, LV_OPA_50, 0);
            lv_obj_set_style_radius(zone, 0, 0);
            lv_obj_set_style_border_width(zone, 0, 0);
            lv_obj_set_style_pad_all(zone, 0, 0);
            lv_obj_clear_flag(zone, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            st->marker_zone = zone;
        }
        if (cfg->marker_width > 0) {
            lv_obj_t* line = lv_obj_create(bar_bg);
            lv_obj_set_size(line, 0, 0);
            lv_obj_set_style_bg_color(line, resolve_lv_color(cfg->marker_color, 0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(line, 0, 0);
            lv_obj_set_style_border_width(line, 0, 0);
            lv_obj_set_style_pad_all(line, 0, 0);
            lv_obj_clear_flag(line, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            st->marker_line = line;
        }
    }
}

// ---- Animation exec callback for bar fill dimension ----
static void bar_chart_anim_cb(void* var, int32_t value) {
    auto* st = reinterpret_cast<BarChartState*>(var);
    if (!st->bar_fill) return;
    st->last_anim_px = (int16_t)value;
    if (st->zero_centered) {
        // `value` is the value-edge position (px from the min/bottom end).
        // The opposite edge is pinned to the zero baseline.
        int32_t z = st->zero_px;
        int32_t lo = (value < z) ? value : z;
        int32_t hi = (value > z) ? value : z;
        int32_t size = hi - lo;
        if (st->horizontal) {
            lv_obj_set_width(st->bar_fill, size);
            lv_obj_align(st->bar_fill, LV_ALIGN_LEFT_MID, lo, 0);
        } else {
            lv_obj_set_height(st->bar_fill, size);
            lv_obj_align(st->bar_fill, LV_ALIGN_BOTTOM_MID, 0, -lo);
        }
    } else if (st->horizontal) {
        lv_obj_set_width(st->bar_fill, value);
        lv_obj_align(st->bar_fill, LV_ALIGN_LEFT_MID, 0, 0);
    } else {
        lv_obj_set_height(st->bar_fill, value);
        lv_obj_align(st->bar_fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
}

// ---- Helper: position the target marker line + zone band ----
static void bar_chart_position_marker(BarChartState* st, const BarChartConfig* cfg,
                                      float mn, float mx, float marker_val) {
    float range = mx - mn;
    if (range <= 0.0f) range = 1.0f;
    float ratio = (marker_val - mn) / range;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    const int16_t track_total = st->horizontal
        ? (int16_t)lv_obj_get_width(st->bar_bg)
        : (int16_t)lv_obj_get_height(st->bar_bg);
    const int16_t track_cross = st->horizontal
        ? (int16_t)lv_obj_get_height(st->bar_bg)
        : (int16_t)lv_obj_get_width(st->bar_bg);
    const int16_t px = (int16_t)roundf(ratio * track_total);

    // Zone band centered on the marker
    if (st->marker_zone && cfg->marker_zone_pct > 0) {
        float half = (cfg->marker_zone_pct / 100.0f) / 2.0f;
        float lo_r = ratio - half;
        float hi_r = ratio + half;
        if (lo_r < 0.0f) lo_r = 0.0f;
        if (hi_r > 1.0f) hi_r = 1.0f;
        int16_t lo_px = (int16_t)roundf(lo_r * track_total);
        int16_t hi_px = (int16_t)roundf(hi_r * track_total);
        int16_t band = hi_px - lo_px;
        if (band < 1) band = 1;
        if (st->horizontal) {
            lv_obj_set_size(st->marker_zone, band, track_cross);
            lv_obj_align(st->marker_zone, LV_ALIGN_LEFT_MID, lo_px, 0);
        } else {
            lv_obj_set_size(st->marker_zone, track_cross, band);
            lv_obj_align(st->marker_zone, LV_ALIGN_BOTTOM_MID, 0, -lo_px);
        }
    }

    // Marker line centered on the marker position
    if (st->marker_line && cfg->marker_width > 0) {
        if (st->horizontal) {
            lv_obj_set_size(st->marker_line, cfg->marker_width, track_cross);
            lv_obj_align(st->marker_line, LV_ALIGN_LEFT_MID, px - cfg->marker_width / 2, 0);
        } else {
            lv_obj_set_size(st->marker_line, track_cross, cfg->marker_width);
            lv_obj_align(st->marker_line, LV_ALIGN_BOTTOM_MID, 0, cfg->marker_width / 2 - px);
        }
    }
}

static void bar_chart_update(lv_obj_t* tile, const WidgetConfig* wcfg,
                              WidgetState* state, const char* raw_value) {
    auto* cfg = reinterpret_cast<const BarChartConfig*>(wcfg->data);
    auto* st = reinterpret_cast<BarChartState*>(state->data);

    if (!st->bar_fill || !st->bar_bg) return;

    // Parse numeric value from string
    char* end = nullptr;
    float value = strtof(raw_value, &end);
    if (end == raw_value) return; // Not a number

    // Skip redundant updates (within 0.001 tolerance)
    if (!isnan(st->last_value) && fabsf(value - st->last_value) < 0.001f) return;
    st->last_value = value;

    // Resolve bindable min/max
    float bar_max = resolve_number(cfg->bar_max, 3.0f);
    float bar_min = resolve_number(cfg->bar_min, 0.0f);
    st->cached_min = bar_min;
    st->cached_max = bar_max;

    // Compute fill ratio
    float range = bar_max - bar_min;
    if (range <= 0.0f) range = 1.0f;

    int16_t bar_total = cfg->horizontal ? lv_obj_get_width(st->bar_bg) : lv_obj_get_height(st->bar_bg);

    // `target_px` is the animated quantity: in normal mode it is the fill
    // dimension; in zero-centered mode it is the value-edge position.
    int16_t target_px;
    if (cfg->zero_centered) {
        float val_ratio = (value - bar_min) / range;
        if (val_ratio < 0.0f) val_ratio = 0.0f;
        if (val_ratio > 1.0f) val_ratio = 1.0f;
        float zero_ratio = (0.0f - bar_min) / range;
        if (zero_ratio < 0.0f) zero_ratio = 0.0f;
        if (zero_ratio > 1.0f) zero_ratio = 1.0f;
        st->zero_px = (int16_t)roundf(zero_ratio * bar_total);
        target_px = (int16_t)roundf(val_ratio * bar_total);
    } else {
        float ratio = (fabsf(value) - bar_min) / range;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        target_px = (int16_t)(ratio * bar_total);
        if (target_px == 0 && fabsf(value) > 0.001f) target_px = 1;
    }

    // Current animated quantity (animation start point)
    int16_t cur_px = st->last_anim_px;

    // Skip animation when values arrive faster than the animation duration.
    // Rapid restarts cause the ease-out curve to reset before visible progress,
    // making the bar appear stuck.
    uint32_t now = lv_tick_get();
    bool rapid_update = st->has_received_data &&
                        (now - st->last_update_ms) < (uint32_t)cfg->anim_ms;
    st->last_update_ms = now;

    bool should_animate = st->has_received_data && cfg->anim_ms > 0
                          && cur_px != target_px && !rapid_update;
    st->has_received_data = true;

    if (should_animate) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, st);
        lv_anim_set_values(&a, cur_px, target_px);
        lv_anim_set_duration(&a, cfg->anim_ms);
        lv_anim_set_exec_cb(&a, bar_chart_anim_cb);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    } else {
        bar_chart_anim_cb(st, target_px);
    }

    // Apply bar fill color (uses shared cache with tick)
    lv_color_t clr;
    if (resolve_color_changed(cfg->bar_color, 0x4CAF50, &st->cached_bar_color, &clr))
        lv_obj_set_style_bg_color(st->bar_fill, clr, 0);

}

// ---- Tick: re-resolve binding-driven colors (skip if unchanged) ----

static void bar_chart_tick(lv_obj_t* tile, const WidgetConfig* wcfg,
                           WidgetState* state) {
    auto* cfg = reinterpret_cast<const BarChartConfig*>(wcfg->data);
    auto* st = reinterpret_cast<BarChartState*>(state->data);
    if (!st->bar_fill || !st->bar_bg) return;

    // Re-resolve bindable min/max; invalidate last_value so next update re-applies
    float mn = resolve_number(cfg->bar_min, 0.0f);
    float mx = resolve_number(cfg->bar_max, 3.0f);
    bool range_changed = (mn != st->cached_min || mx != st->cached_max);
    if (range_changed) {
        st->cached_min = mn;
        st->cached_max = mx;
        st->last_value = NAN;
    }

    lv_color_t clr;
    if (resolve_color_changed(cfg->bar_bg_color, 0x1A1A1A, &st->cached_bar_bg_color, &clr))
        lv_obj_set_style_bg_color(st->bar_bg, clr, 0);
    if (resolve_color_changed(cfg->bar_color, 0x4CAF50, &st->cached_bar_color, &clr))
        lv_obj_set_style_bg_color(st->bar_fill, clr, 0);

    // Gridline color (uses cached pointers — no child scan)
    if (st->tick_lines && st->tick_line_count > 0) {
        if (resolve_color_changed(cfg->tick_color, 0x808080, &st->cached_tick_color, &clr)) {
            for (uint8_t i = 0; i < st->tick_line_count; i++)
                lv_obj_set_style_bg_color(st->tick_lines[i], clr, 0);
        }
    }

    // Target marker position + color
    if (cfg->marker_value[0] && (st->marker_line || st->marker_zone)) {
        float marker_val = resolve_number(cfg->marker_value, NAN);
        bool val_changed = isnan(st->cached_marker_val) || fabsf(marker_val - st->cached_marker_val) > 0.001f;
        if (!isnan(marker_val) && (val_changed || range_changed)) {
            st->cached_marker_val = marker_val;
            bar_chart_position_marker(st, cfg, mn, mx, marker_val);
        }
        if (st->marker_line &&
            resolve_color_changed(cfg->marker_color, 0xFFFFFF, &st->cached_marker_color, &clr))
            lv_obj_set_style_bg_color(st->marker_line, clr, 0);
        if (st->marker_zone &&
            resolve_color_changed(cfg->marker_zone_color, 0xFF5722, &st->cached_marker_zone_color, &clr))
            lv_obj_set_style_bg_color(st->marker_zone, clr, 0);
    }
}

static void bar_chart_destroy(WidgetState* state) {
    auto* st = reinterpret_cast<BarChartState*>(state->data);
    // Null bar_fill so any in-flight animation callback bails out.
    st->bar_fill = nullptr;
    // Free the heap-allocated gridline pointer array (the LVGL objects
    // themselves are deleted automatically with the parent tile).
    if (st->tick_lines) {
        lv_free(st->tick_lines);
        st->tick_lines = nullptr;
    }
}

// ---- Registration ----

REGISTER_WIDGET(bar_chart, nullptr, false);

#endif // HAS_DISPLAY
