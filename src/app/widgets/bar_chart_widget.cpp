#include "widget.h"

#if HAS_DISPLAY

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
    float    bar_max;              // Full-scale value (default 3.0)
    float    bar_min;              // Minimum value (default 0.0)
    uint32_t color_good_rgb;      // Color tier 0: below threshold 1
    uint32_t color_ok_rgb;        // Color tier 1: threshold 1 → 2
    uint32_t color_attention_rgb; // Color tier 2: threshold 2 → 3
    uint32_t color_warning_rgb;   // Color tier 3: at/above threshold 3
    float    threshold_1;         // Breakpoint tier 0 → 1
    float    threshold_2;         // Breakpoint tier 1 → 2
    float    threshold_3;         // Breakpoint tier 2 → 3
    bool     use_absolute;        // true = compare |value| (default); false = signed
    uint32_t bar_bg_color_rgb;    // Bar track background (default 0x1A1A1A)
    uint8_t  bar_width_pct;       // Bar width as % of tile width (1-100, default 100)
};

static_assert(sizeof(BarChartConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "BarChartConfig exceeds WIDGET_CONFIG_MAX_BYTES");

// ---- Runtime state (packed into WidgetState.data[]) ----

struct BarChartState {
    lv_obj_t* bar_bg;    // Bar background rectangle
    lv_obj_t* bar_fill;  // Bar fill rectangle (grows from bottom)
    float     last_value; // Last numeric value (for skipping redundant updates)
};

static_assert(sizeof(BarChartState) <= WIDGET_STATE_MAX_BYTES,
              "BarChartState exceeds WIDGET_STATE_MAX_BYTES");

// ---- Helper: pick color tier based on value ----

static lv_color_t pick_tier_color(const BarChartConfig* cfg, float value) {
    float cmp = cfg->use_absolute ? fabsf(value) : value;
    uint32_t rgb;
    if (cmp >= cfg->threshold_3) rgb = cfg->color_warning_rgb;
    else if (cmp >= cfg->threshold_2) rgb = cfg->color_attention_rgb;
    else if (cmp >= cfg->threshold_1) rgb = cfg->color_ok_rgb;
    else rgb = cfg->color_good_rgb;
    return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// ---- Parse color helper (same logic as pad_config.cpp) ----

static uint32_t parse_widget_color(JsonVariant v, uint32_t def) {
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

// ---- WidgetType callbacks ----

static void bar_chart_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<BarChartConfig*>(data);
    memset(cfg, 0, sizeof(BarChartConfig));

    cfg->bar_max = btn["widget_bar_max"] | 3.0f;
    cfg->bar_min = btn["widget_bar_min"] | 0.0f;
    cfg->use_absolute = btn["widget_use_absolute"] | true;

    // Color tiers with energy-monitor-inspired defaults
    cfg->color_good_rgb     = parse_widget_color(btn["widget_color_good"],      0x4CAF50); // green
    cfg->color_ok_rgb       = parse_widget_color(btn["widget_color_ok"],        0x8BC34A); // light green
    cfg->color_attention_rgb = parse_widget_color(btn["widget_color_attention"], 0xFF9800); // orange
    cfg->color_warning_rgb  = parse_widget_color(btn["widget_color_warning"],   0xF44336); // red

    cfg->bar_bg_color_rgb = parse_widget_color(btn["widget_bar_bg_color"], 0x1A1A1A);
    uint8_t wpct = btn["widget_bar_width_pct"] | (uint8_t)100;
    cfg->bar_width_pct = (wpct < 1) ? 1 : (wpct > 100) ? 100 : wpct;

    // Thresholds default to even thirds of bar_max
    float range = cfg->bar_max - cfg->bar_min;
    cfg->threshold_1 = btn["widget_threshold_1"] | (cfg->bar_min + range * 0.33f);
    cfg->threshold_2 = btn["widget_threshold_2"] | (cfg->bar_min + range * 0.66f);
    cfg->threshold_3 = btn["widget_threshold_3"] | (cfg->bar_min + range * 0.90f);
}

static void bar_chart_create(lv_obj_t* tile, const WidgetConfig* wcfg,
                              const ScreenButtonConfig* btn,
                              const PadRect* rect, const UIScaleInfo* scale,
                              lv_obj_t* icon_img, WidgetState* state) {
    auto* cfg = reinterpret_cast<const BarChartConfig*>(wcfg->data);
    auto* st = reinterpret_cast<BarChartState*>(state->data);
    memset(st, 0, sizeof(BarChartState));
    st->last_value = NAN;

    // Only reserve space for labels that are actually used (static text or MQTT binding)
    bool has_top = btn->label_top[0] || btn->label_top_bind.mqtt_topic[0];
    bool has_bot = btn->label_bottom[0] || btn->label_bottom_bind.mqtt_topic[0];
    int16_t label_h = lv_font_get_line_height(scale->font_small) + 2;
    int16_t top_h = has_top ? label_h : 0;
    int16_t bot_h = has_bot ? label_h : 0;
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
        lv_obj_align(icon_img, LV_ALIGN_TOP_MID, 0, top_h);
    }

    // Bar top position: right below icon + gap (all from top, no guessing)
    int16_t bar_top = top_h + icon_h + gap;
    int16_t bar_bottom_margin = bot_h + 4;  // space for bottom label
    // Ask LVGL for the actual content height (accounts for padding + border)
    lv_obj_update_layout(tile);
    int16_t content_h = (int16_t)lv_obj_get_content_height(tile);
    int16_t bar_h = content_h - bar_top - bar_bottom_margin;
    if (bar_h < 8) bar_h = 8;
    int16_t max_bar_w = rect->w - 16; // 8px margin each side
    int16_t bar_w = (int16_t)(max_bar_w * cfg->bar_width_pct / 100);
    if (bar_w < 4) bar_w = 4;

    LOGD(TAG, "Layout: rect=%dx%d content_h=%d top_h=%d icon_h=%d gap=%d bar_top=%d bar_h=%d bot_h=%d margin=%d",
         rect->w, rect->h, content_h, top_h, icon_h, gap, bar_top, bar_h, bot_h, bar_bottom_margin);

    // Bar background — positioned from top so gap is guaranteed
    uint32_t bg_rgb = cfg->bar_bg_color_rgb;
    lv_obj_t* bar_bg = lv_obj_create(tile);
    lv_obj_set_size(bar_bg, bar_w, bar_h);
    lv_obj_align(bar_bg, LV_ALIGN_TOP_MID, 0, bar_top);
    lv_obj_set_style_bg_color(bar_bg, lv_color_make((bg_rgb >> 16) & 0xFF, (bg_rgb >> 8) & 0xFF, bg_rgb & 0xFF), 0);
    lv_obj_set_style_bg_opa(bar_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar_bg, 4, 0);
    lv_obj_set_style_border_width(bar_bg, 0, 0);
    lv_obj_set_style_pad_all(bar_bg, 0, 0);
    lv_obj_clear_flag(bar_bg, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    // Bar fill (grows from bottom)
    lv_obj_t* bar_fill = lv_obj_create(bar_bg);
    lv_obj_set_size(bar_fill, bar_w, 0); // start at 0 height
    lv_obj_align(bar_fill, LV_ALIGN_BOTTOM_MID, 0, 0);
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

    // Compute fill height
    float range = cfg->bar_max - cfg->bar_min;
    if (range <= 0.0f) range = 1.0f;
    float ratio = (fabsf(value) - cfg->bar_min) / range;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    int16_t bar_total_h = lv_obj_get_height(st->bar_bg);
    int16_t fill_h = (int16_t)(ratio * bar_total_h);
    // Ensure at least 1px fill for non-zero values
    if (fill_h == 0 && fabsf(value) > 0.001f) fill_h = 1;

    lv_obj_set_height(st->bar_fill, fill_h);
    lv_obj_align(st->bar_fill, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Pick color tier
    lv_color_t color = pick_tier_color(cfg, value);
    lv_obj_set_style_bg_color(st->bar_fill, color, 0);
}

static void bar_chart_destroy(WidgetState* state) {
    // LVGL objects are children of the tile — deleted automatically.
    // Nothing extra to free.
    (void)state;
}

// ---- Registration ----

const WidgetType bar_chart_widget_type = {
    "bar_chart",
    bar_chart_parse,
    bar_chart_create,
    bar_chart_update,
    bar_chart_destroy
};

#endif // HAS_DISPLAY
