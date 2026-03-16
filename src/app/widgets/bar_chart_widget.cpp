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
};

static_assert(sizeof(BarChartConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "BarChartConfig exceeds WIDGET_CONFIG_MAX_BYTES");

// ---- Runtime state (packed into WidgetState.data[]) ----

struct BarChartState {
    lv_obj_t* bar_bg;    // Bar background rectangle
    lv_obj_t* bar_fill;  // Bar fill rectangle (grows from bottom)
    float     last_value; // Last numeric value (for skipping redundant updates)
    float     cached_min; // Last resolved min (for detecting binding changes in tick)
    float     cached_max; // Last resolved max (for detecting binding changes in tick)
    uint32_t  cached_bar_bg_color;  // Last resolved bar background color
    uint32_t  cached_bar_color;     // Last resolved bar fill color
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
}

static void bar_chart_create(lv_obj_t* tile, const WidgetConfig* wcfg,
                              const ScreenButtonConfig* btn,
                              const PadRect* rect, const UIScaleInfo* scale,
                              lv_obj_t* icon_img, WidgetState* state) {
    auto* cfg = reinterpret_cast<const BarChartConfig*>(wcfg->data);
    auto* st = reinterpret_cast<BarChartState*>(state->data);
    memset(st, 0, sizeof(BarChartState));
    st->last_value = NAN;
    st->cached_min = NAN;
    st->cached_max = NAN;
    st->cached_bar_bg_color = COLOR_CACHE_INIT;
    st->cached_bar_color    = COLOR_CACHE_INIT;

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
    int16_t gap = 6;  // margin between icon and bar

    // Position icon right below the top label
    if (icon_img) {
        lv_obj_align(icon_img, LV_ALIGN_TOP_MID, ui_ofs_x, top_h + ui_ofs_y);
    }

    // Ask LVGL for the actual content dimensions (accounts for padding + border)
    lv_obj_update_layout(tile);
    int16_t content_h = (int16_t)lv_obj_get_content_height(tile);
    int16_t content_w = (int16_t)lv_obj_get_content_width(tile);

    int16_t bar_w, bar_h, bar_top;
    int16_t bar_bottom_margin;

    if (cfg->horizontal) {
        // Horizontal: bar spans the full width, height controlled by bar_width_pct
        int16_t bar_top_start = top_h + icon_h + (icon_img ? gap : 0);
        bar_bottom_margin = bot_h + 4;
        int16_t avail_h = content_h - bar_top_start - bar_bottom_margin;
        if (avail_h < 8) avail_h = 8;
        bar_h = (int16_t)(avail_h * cfg->bar_width_pct / 100);
        if (bar_h < 4) bar_h = 4;
        bar_w = content_w;  // full content width
        bar_top = bar_top_start + (avail_h - bar_h) / 2; // vertically center in available space
    } else {
        // Vertical (original): bar spans the full height, width controlled by bar_width_pct
        bar_top = top_h + icon_h + gap;
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

    // Compute fill height
    float range = bar_max - bar_min;
    if (range <= 0.0f) range = 1.0f;
    float ratio = (fabsf(value) - bar_min) / range;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    if (cfg->horizontal) {
        int16_t bar_total_w = lv_obj_get_width(st->bar_bg);
        int16_t fill_w = (int16_t)(ratio * bar_total_w);
        if (fill_w == 0 && fabsf(value) > 0.001f) fill_w = 1;
        lv_obj_set_width(st->bar_fill, fill_w);
        lv_obj_align(st->bar_fill, LV_ALIGN_LEFT_MID, 0, 0);
    } else {
        int16_t bar_total_h = lv_obj_get_height(st->bar_bg);
        int16_t fill_h = (int16_t)(ratio * bar_total_h);
        if (fill_h == 0 && fabsf(value) > 0.001f) fill_h = 1;
        lv_obj_set_height(st->bar_fill, fill_h);
        lv_obj_align(st->bar_fill, LV_ALIGN_BOTTOM_MID, 0, 0);
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
    if (mn != st->cached_min || mx != st->cached_max) {
        st->cached_min = mn;
        st->cached_max = mx;
        st->last_value = NAN;
    }

    lv_color_t clr;
    if (resolve_color_changed(cfg->bar_bg_color, 0x1A1A1A, &st->cached_bar_bg_color, &clr))
        lv_obj_set_style_bg_color(st->bar_bg, clr, 0);
    if (resolve_color_changed(cfg->bar_color, 0x4CAF50, &st->cached_bar_color, &clr))
        lv_obj_set_style_bg_color(st->bar_fill, clr, 0);
}

static void bar_chart_destroy(WidgetState* state) {
    // LVGL objects are children of the tile — deleted automatically.
    // Nothing extra to free.
    (void)state;
}

// ---- Registration ----

REGISTER_WIDGET(bar_chart, nullptr);

#endif // HAS_DISPLAY
