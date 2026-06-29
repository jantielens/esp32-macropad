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
    char     bar_color[CONFIG_COLOR_MAX_LEN];            // Bar 1 fill color (bindable, default "#4CAF50")
    char     bar_color_2[CONFIG_COLOR_MAX_LEN];          // Bar 2 fill color (bindable, default "#2196F3")
    char     bar_color_3[CONFIG_COLOR_MAX_LEN];          // Bar 3 fill color (bindable, default "#9C27B0")
    char     bar_color_4[CONFIG_COLOR_MAX_LEN];          // Bar 4 fill color (bindable, default "#FF9800")
    char     bar_label[CONFIG_BINDABLE_SHORT_LEN];       // Bar 1 caption (bindable, "" = none)
    char     bar_label_2[CONFIG_BINDABLE_SHORT_LEN];     // Bar 2 caption (bindable, "" = none)
    char     bar_label_3[CONFIG_BINDABLE_SHORT_LEN];     // Bar 3 caption (bindable, "" = none)
    char     bar_label_4[CONFIG_BINDABLE_SHORT_LEN];     // Bar 4 caption (bindable, "" = none)
    char     bar_bg_color[CONFIG_BINDABLE_SHORT_LEN];    // Bar track background (default "#1A1A1A")
    uint8_t  bar_width_pct;       // Per-bar thickness as % of its column (1-100, default 100)
    uint8_t  bar_label_size;      // Caption strip size in px: width (horizontal) / height (vertical), 0 = auto
    bool     horizontal;          // true = horizontal bar (grows left→right)
    bool     zero_centered;       // Fill from the zero baseline instead of min (default false)
    bool     dual_binding_pair_1; // Slots 1/2 share one center-anchored bar (pos/neg)
    bool     dual_binding_pair_2; // Slots 3/4 share one center-anchored bar (pos/neg)
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

// Default fill colors per bar slot (mirrors the gauge widget's ring defaults).
static const uint32_t BAR_SLOT_DEFAULT_COLOR[MAX_WIDGET_BINDINGS] = {
    0x4CAF50, 0x2196F3, 0x9C27B0, 0xFF9800
};

// ---- Runtime state (packed into WidgetState.data[]) ----

struct BarChartState;

// Per-bar runtime state. One slot per active data binding (1–4 bars). Each bar
// owns its own track (background), fill, gridlines, and target marker/zone so
// nothing spans across bars — mirroring the gauge widget's per-ring model.
struct BarSlot {
    BarChartState* owner;       // Back-pointer (animation callback needs widget-wide flags)
    lv_obj_t* bg;               // Per-bar background/track rectangle (or nullptr)
    lv_obj_t* fill;             // Bar fill rectangle (child of bg, or nullptr)
    lv_obj_t* marker_line;      // Per-bar target marker line (or nullptr)
    lv_obj_t* marker_zone;      // Per-bar target zone band overlay (or nullptr)
    lv_obj_t* label;            // Per-bar caption (beneath column / left of row, or nullptr)
    lv_obj_t* cap;              // Dual bars only: square overlay on the inner (center) edge (or nullptr)
    float     last_value;       // Last numeric value (for skipping redundant updates)
    uint32_t  cached_color;     // Last resolved fill color
    int16_t   last_anim_px;     // Last animated quantity (fill px or value-edge px)
    int16_t   track_total;      // Track length along the fill direction
    int16_t   track_cross;      // Track length on the cross axis (= fill thickness)
    int8_t    dual_dir;         // 0 = normal; -1 grows toward min (near); +1 toward max (far)
};

struct BarChartState {
    BarSlot   slots[MAX_WIDGET_BINDINGS]; // Per-bar state (track + fill + marker)
    lv_obj_t** tick_lines; // Heap-allocated gridline pointers, flat across all bars (or nullptr)
    float     cached_min; // Last resolved min (for detecting binding changes in tick)
    float     cached_max; // Last resolved max (for detecting binding changes in tick)
    float     cached_marker_val;    // Last resolved marker position
    uint32_t  cached_bar_bg_color;  // Last resolved bar background color
    uint32_t  cached_tick_color;    // Last resolved gridline color
    uint32_t  cached_marker_color;  // Last resolved marker line color
    uint32_t  cached_marker_zone_color; // Last resolved zone band color
    uint32_t  last_update_ms;       // Timestamp of last update (rapid-change detection)
    int16_t   zero_px;              // Zero baseline position in px (zero-centered mode)
    uint8_t   num_bars;             // Number of active bars (1–MAX_WIDGET_BINDINGS)
    uint8_t   tick_line_count;      // Number of cached gridline pointers (all bars)
    bool      has_received_data;    // True after first value received (snap on first)
    bool      horizontal;           // Cached orientation for anim callback
    bool      zero_centered;        // Cached zero-centered mode for anim callback
    bool      dual_pair_1_active;   // Slots 0/1 rendered as one center-anchored bar
    bool      dual_pair_2_active;   // Slots 2/3 rendered as one center-anchored bar
};

static_assert(sizeof(BarChartState) <= WIDGET_STATE_MAX_BYTES,
              "BarChartState exceeds WIDGET_STATE_MAX_BYTES");

// Resolve a bar slot's configured fill color string (slot 0 = bar_color, etc.).
static const char* bar_slot_color_cfg(const BarChartConfig* cfg, uint8_t idx) {
    switch (idx) {
        case 1:  return cfg->bar_color_2;
        case 2:  return cfg->bar_color_3;
        case 3:  return cfg->bar_color_4;
        default: return cfg->bar_color;
    }
}

// Resolve a bar slot's configured caption template (slot 0 = bar_label, etc.).
static const char* bar_slot_label_cfg(const BarChartConfig* cfg, uint8_t idx) {
    switch (idx) {
        case 1:  return cfg->bar_label_2;
        case 2:  return cfg->bar_label_3;
        case 3:  return cfg->bar_label_4;
        default: return cfg->bar_label;
    }
}

// Resolve a caption template (binding or static text) into a label's text.
// Mirrors the gauge widget's start-label resolution.
static void bar_chart_set_label_text(lv_obj_t* label, const char* templ) {
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
    // Decode literal "\n" escape sequences into real newlines (multi-line captions).
    char* r = resolved;
    char* w = resolved;
    while (*r) {
        if (r[0] == '\\' && r[1] == 'n') { *w++ = '\n'; r += 2; }
        else { *w++ = *r++; }
    }
    *w = '\0';
    if (strcmp(lv_label_get_text(label), resolved) != 0) {
        lv_label_set_text(label, resolved);
    }
}


// ---- WidgetType callbacks ----

static void bar_chart_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<BarChartConfig*>(data);
    memset(cfg, 0, sizeof(BarChartConfig));

    widget_parse_field(btn["widget_bar_min"],      cfg->bar_min,       sizeof(cfg->bar_min),       "0", false);
    widget_parse_field(btn["widget_bar_max"],      cfg->bar_max,       sizeof(cfg->bar_max),       "3", false);

    widget_parse_field(btn["widget_bar_color"],     cfg->bar_color,     sizeof(cfg->bar_color),     "#4CAF50");
    widget_parse_field(btn["widget_bar_color_2"],   cfg->bar_color_2,   sizeof(cfg->bar_color_2),   "#2196F3");
    widget_parse_field(btn["widget_bar_color_3"],   cfg->bar_color_3,   sizeof(cfg->bar_color_3),   "#9C27B0");
    widget_parse_field(btn["widget_bar_color_4"],   cfg->bar_color_4,   sizeof(cfg->bar_color_4),   "#FF9800");

    widget_parse_field(btn["widget_bar_label"],     cfg->bar_label,     sizeof(cfg->bar_label),     "", false);
    widget_parse_field(btn["widget_bar_label_2"],   cfg->bar_label_2,   sizeof(cfg->bar_label_2),   "", false);
    widget_parse_field(btn["widget_bar_label_3"],   cfg->bar_label_3,   sizeof(cfg->bar_label_3),   "", false);
    widget_parse_field(btn["widget_bar_label_4"],   cfg->bar_label_4,   sizeof(cfg->bar_label_4),   "", false);
    widget_parse_field(btn["widget_bar_bg_color"],  cfg->bar_bg_color,  sizeof(cfg->bar_bg_color),  "#1A1A1A");
    uint8_t wpct = btn["widget_bar_width_pct"] | (uint8_t)100;
    cfg->bar_width_pct = clamp_val<uint8_t>(wpct, 1, 100);

    // Caption strip size: width (horizontal) or height (vertical). 0 = auto.
    uint8_t blsz = btn["widget_bar_label_size"] | (uint8_t)0;
    cfg->bar_label_size = (blsz > 200) ? 200 : blsz;

    const char* orient = btn["widget_orientation"] | "";
    cfg->horizontal = (orient[0] == 'h' || orient[0] == 'H');

    cfg->zero_centered = btn["widget_bar_zero_centered"] | false;
    cfg->dual_binding_pair_1 = btn["widget_bar_dual_binding_pair_1"] | false;
    cfg->dual_binding_pair_2 = btn["widget_bar_dual_binding_pair_2"] | false;

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

// Center cap for a dual bar: a small square child of the fill that overlays the
// rounded inner corners (the edge meeting the partner at the zero baseline), so
// the two halves touch with a flat seam while their outer ends stay rounded. As
// a child of the fill it stays anchored to the fixed center edge automatically.
// Anchor the cap to the fill's baseline (zero) edge. `dir < 0` means the bar
// grows toward min/near (baseline edge is right/top); otherwise toward max/far
// (baseline edge is left/bottom). Zero-centered single bars flip `dir` with the
// sign of their value, so this is called again from the anim callback.
static void bar_chart_align_center_cap(lv_obj_t* cap, bool horizontal, int8_t dir) {
    if (horizontal) {
        lv_obj_align(cap, dir < 0 ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, 0, 0);
    } else {
        lv_obj_align(cap, dir < 0 ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0, 0);
    }
}

static lv_obj_t* bar_chart_make_center_cap(lv_obj_t* fill, bool horizontal,
                                           int8_t dual_dir, int16_t track_cross,
                                           lv_color_t color) {
    const int16_t R = 4;  // matches the fill corner radius
    lv_obj_t* cap = lv_obj_create(fill);
    if (horizontal) {
        lv_obj_set_size(cap, R, track_cross);
    } else {
        lv_obj_set_size(cap, track_cross, R);
    }
    bar_chart_align_center_cap(cap, horizontal, dual_dir);
    lv_obj_set_style_bg_color(cap, color, 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cap, 0, 0);
    lv_obj_set_style_border_width(cap, 0, 0);
    lv_obj_set_style_pad_all(cap, 0, 0);
    lv_obj_clear_flag(cap, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    return cap;
}

static void bar_chart_create(lv_obj_t* tile, const WidgetConfig* wcfg,
                              const ScreenButtonConfig* btn,
                              const PadRect* rect, const UIScaleInfo* scale,
                              lv_obj_t* icon_img, lv_obj_t* center_label,
                              WidgetState* state) {
    auto* cfg = reinterpret_cast<const BarChartConfig*>(wcfg->data);
    auto* st = reinterpret_cast<BarChartState*>(state->data);
    memset(st, 0, sizeof(BarChartState));
    st->cached_min = NAN;
    st->cached_max = NAN;
    st->cached_marker_val = NAN;
    st->cached_bar_bg_color = COLOR_CACHE_INIT;
    st->cached_tick_color   = COLOR_CACHE_INIT;
    st->cached_marker_color = COLOR_CACHE_INIT;
    st->cached_marker_zone_color = COLOR_CACHE_INIT;
    for (uint8_t i = 0; i < MAX_WIDGET_BINDINGS; i++) {
        st->slots[i].owner = st;
        st->slots[i].last_value = NAN;
        st->slots[i].cached_color = COLOR_CACHE_INIT;
    }

    // Count active bars: highest non-empty data binding index + 1 (slot 0 is
    // always present). Empty intermediate slots are preserved so value→bar
    // mapping stays aligned with the tab-packed update payload.
    uint8_t num_bars = 1;
    for (uint8_t i = 1; i < MAX_WIDGET_BINDINGS; i++) {
        if (wcfg->data_binding[i][0]) num_bars = i + 1;
    }
    st->num_bars = num_bars;

    // ---- Dual binding: collapse a slot pair into one center-anchored bar ----
    // Pair 1 = slots 0(near)/1(far), pair 2 = slots 2(near)/3(far). A pair is
    // active only when its partner slot is bound. The near slot owns the shared
    // track and keeps the normal caption side (left/bottom), growing toward min;
    // the far slot's fill is hosted inside the same track and grows toward max,
    // with its caption on the opposite end (right/top) — mirrors the gauge's
    // dual ring. Each half is scaled on the full min..max scale with 0 fixed at
    // the zero baseline, so a symmetric look needs min = -max.
    st->dual_pair_1_active = cfg->dual_binding_pair_1 && wcfg->data_binding[1][0] && num_bars >= 2;
    st->dual_pair_2_active = cfg->dual_binding_pair_2 && wcfg->data_binding[3][0] && num_bars >= 4;
    if (st->dual_pair_1_active) { st->slots[0].dual_dir = -1; st->slots[1].dual_dir = +1; }
    if (st->dual_pair_2_active) { st->slots[2].dual_dir = -1; st->slots[3].dual_dir = +1; }

    // Visual track list: every slot except the far(+1) partner of an active pair
    // owns its own track. The partner shares its owner's track.
    uint8_t vbar_owner[MAX_WIDGET_BINDINGS];
    uint8_t vbars = 0;
    for (uint8_t i = 0; i < num_bars; i++) {
        if (st->slots[i].dual_dir == +1) continue;  // far partner: no own track
        vbar_owner[vbars++] = i;
    }
    if (vbars == 0) vbar_owner[vbars++] = 0;  // safety

    // Only reserve space for labels that are actually used (static text or MQTT binding)
    bool has_top = btn->label_top[0];
    bool has_bot = btn->label_bottom[0];
    int16_t label_h = lv_font_get_line_height(scale->font_small) + 2;
    int16_t top_h = has_top ? label_h : 0;
    int16_t bot_h = has_bot ? label_h : 0;

    // Per-bar captions reserve a strip beneath each column (vertical) or to the
    // left of each row (horizontal). Only reserved when at least one active bar
    // has a caption, so caption-less charts keep their full bar extent. A dual
    // pair's far slot reserves a second strip on the opposite end (right/top).
    bool has_near_labels = false;  // owner/normal slot captions (left/bottom strip)
    bool has_far_labels = false;   // dual far-partner captions (right/top strip)
    for (uint8_t i = 0; i < num_bars; i++) {
        if (!bar_slot_label_cfg(cfg, i)[0]) continue;
        if (st->slots[i].dual_dir == +1) has_far_labels = true;
        else                             has_near_labels = true;
    }
    bool has_labels = has_near_labels || has_far_labels;
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

    // ------------------------------------------------------------------
    // Per-bar layout. Each bar gets its own track (background) so the
    // background, gridlines, and target marker stay scoped to a single bar
    // instead of spanning the whole widget (mirrors the gauge's per-ring
    // model). The N tracks are spread evenly across the full available extent
    // — columns when vertical, rows when horizontal — with a fixed 6 px gap.
    // bar_width_pct then sets each bar's thickness within its own column/row.
    // A single bar fills the full extent exactly as before (backward
    // compatible: column == full width, gap collapses to 0).
    const uint8_t n = vbars;  // number of visual tracks (a dual pair counts as 1)
    const int16_t COL_GAP = (n > 1) ? 6 : 0;

    // Caption strip dimensions (0 when no captions are configured). A dual pair
    // adds a second strip on the far end: right when horizontal, top when vertical.
    int16_t hlabel_w = 0;      // horizontal: left (near) caption strip width
    int16_t hlabel_w_far = 0;  // horizontal: right (far) caption strip width
    int16_t vlabel_h = 0;      // vertical: bottom (near) caption strip height
    int16_t vlabel_h_far = 0;  // vertical: top (far) caption strip height
    if (has_labels) {
        if (cfg->horizontal) {
            int16_t w = cfg->bar_label_size > 0
                            ? cfg->bar_label_size
                            : clamp_val<int16_t>((int16_t)(content_w * 28 / 100), 30, 120);
            if (has_near_labels) hlabel_w = w;
            if (has_far_labels)  hlabel_w_far = w;
        } else {
            int16_t h = cfg->bar_label_size > 0 ? (int16_t)cfg->bar_label_size
                                                : (int16_t)(label_h + 4);
            if (has_near_labels) vlabel_h = h;
            if (has_far_labels)  vlabel_h_far = h;
        }
    }

    int16_t bar_top, bar_bottom_margin;
    int16_t fill_len;     // track length along the fill direction (shared by all bars)
    int16_t band_full;    // full cross extent available to the column/row group
    int16_t group_start;  // cross-axis start of the group (horizontal rows only)

    if (cfg->horizontal) {
        // Horizontal: bars span the width (minus caption strips); rows divide height.
        int16_t bar_top_start = top_h + header_h + (header_h > 0 ? gap : 0);
        bar_bottom_margin = bot_h + 4;
        int16_t avail_h = content_h - bar_top_start - bar_bottom_margin;
        if (avail_h < 8) avail_h = 8;
        fill_len = content_w - hlabel_w - hlabel_w_far;
        if (fill_len < 8) fill_len = 8;
        band_full = avail_h;
        group_start = bar_top_start;
        bar_top = bar_top_start;  // (placement is per-row for horizontal)
    } else {
        // Vertical: bars span the height (minus caption strips); columns divide width.
        // bar_top is pushed down by the far (top) strip so its caption has room.
        bar_top = top_h + header_h + gap + vlabel_h_far;
        bar_bottom_margin = bot_h + 4;
        fill_len = content_h - bar_top - bar_bottom_margin - vlabel_h;
        if (fill_len < 8) fill_len = 8;
        band_full = rect->w - 16;  // 8px margin each side
        group_start = 0;
    }
    bar_top += ui_ofs_y;

    // Cross-axis sizing: divide band into N columns/rows, bar is centered in its slot.
    int16_t col_cross = (int16_t)((band_full - COL_GAP * (n - 1)) / n);
    if (col_cross < 4) col_cross = 4;
    int16_t track_cross = (int16_t)(col_cross * cfg->bar_width_pct / 100);
    if (track_cross < 4) track_cross = 4;
    int16_t group_cross = (int16_t)(n * col_cross + (n - 1) * COL_GAP);

    LOGD(TAG, "Layout: rect=%dx%d content_h=%d bar_top=%d fill_len=%d col_cross=%d track_cross=%d bars=%d horiz=%d",
         rect->w, rect->h, content_h, bar_top, fill_len, col_cross, track_cross, n, cfg->horizontal);

    lv_color_t bg_clr = resolve_lv_color(cfg->bar_bg_color, 0x1A1A1A);
    lv_color_t tick_clr = resolve_lv_color(cfg->tick_color, 0x808080);

    // One flat gridline pointer array, tick_count per bar.
    if (cfg->tick_count > 0) {
        st->tick_lines = (lv_obj_t**)lv_malloc(sizeof(lv_obj_t*) * cfg->tick_count * n);
        if (!st->tick_lines) LOGW(TAG, "Failed to alloc tick_lines (%u)", (unsigned)(cfg->tick_count * n));
    }

    for (uint8_t v = 0; v < n; v++) {
        uint8_t oi = vbar_owner[v];     // owner slot index for this visual track
        BarSlot* slot = &st->slots[oi];

        // Per-bar track (background). Vertical: the column group is centered
        // via TOP_MID with a per-column x offset. Horizontal: stacked rows,
        // the row group is centered vertically in the available height.
        lv_obj_t* bg = lv_obj_create(tile);
        lv_obj_set_size(bg, cfg->horizontal ? fill_len : track_cross,
                            cfg->horizontal ? track_cross : fill_len);
        int16_t row_top = 0;       // horizontal: this row's track top (cross axis)
        int16_t col_center_x = 0;  // vertical: this column's center x offset
        if (cfg->horizontal) {
            int16_t group_top = group_start + (band_full - group_cross) / 2;
            row_top = group_top + v * (col_cross + COL_GAP) + (col_cross - track_cross) / 2;
            // Center the track between the left/right caption strips.
            lv_obj_align(bg, LV_ALIGN_TOP_MID, ui_ofs_x + (hlabel_w - hlabel_w_far) / 2, row_top + ui_ofs_y);
        } else {
            col_center_x = (int16_t)(-group_cross / 2 + col_cross / 2 + v * (col_cross + COL_GAP));
            lv_obj_align(bg, LV_ALIGN_TOP_MID, ui_ofs_x + col_center_x, bar_top);
        }
        lv_obj_set_style_bg_color(bg, bg_clr, 0);
        lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bg, 4, 0);
        lv_obj_set_style_border_width(bg, 0, 0);
        lv_obj_set_style_pad_all(bg, 0, 0);
        lv_obj_clear_flag(bg, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

        // Bar fill — fills the track's full cross axis, grows from bottom/left.
        lv_obj_t* fill = lv_obj_create(bg);
        if (cfg->horizontal) {
            lv_obj_set_size(fill, 0, track_cross);
            lv_obj_align(fill, LV_ALIGN_TOP_LEFT, 0, 0);
        } else {
            lv_obj_set_size(fill, track_cross, 0);
            lv_obj_align(fill, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        }
        lv_obj_set_style_bg_color(fill, resolve_lv_color(bar_slot_color_cfg(cfg, oi), BAR_SLOT_DEFAULT_COLOR[oi]), 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(fill, 4, 0);
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_set_style_pad_all(fill, 0, 0);
        lv_obj_clear_flag(fill, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

        slot->bg = bg;
        slot->fill = fill;
        slot->track_total = fill_len;
        slot->track_cross = track_cross;

        // Square off the inner (center/baseline) corners with a cap overlay:
        // dual bars (fixed center seam) and single zero-centered bars (baseline
        // edge re-anchored per value sign in bar_chart_anim_cb).
        if (slot->dual_dir != 0 || cfg->zero_centered) {
            slot->cap = bar_chart_make_center_cap(
                fill, cfg->horizontal,
                slot->dual_dir != 0 ? slot->dual_dir : (int8_t)+1, track_cross,
                resolve_lv_color(bar_slot_color_cfg(cfg, oi), BAR_SLOT_DEFAULT_COLOR[oi]));
        }

        // Dual far partner: a second fill hosted inside the same track. Both
        // halves anchor at the shared zero baseline (see bar_chart_anim_cb); the
        // owner grows toward min, the partner toward max.
        BarSlot* partner = nullptr;
        if (slot->dual_dir == -1 && oi + 1 < MAX_WIDGET_BINDINGS) {
            partner = &st->slots[oi + 1];
            lv_obj_t* pfill = lv_obj_create(bg);
            if (cfg->horizontal) {
                lv_obj_set_size(pfill, 0, track_cross);
                lv_obj_align(pfill, LV_ALIGN_TOP_LEFT, 0, 0);
            } else {
                lv_obj_set_size(pfill, track_cross, 0);
                lv_obj_align(pfill, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            }
            lv_obj_set_style_bg_color(pfill, resolve_lv_color(bar_slot_color_cfg(cfg, oi + 1), BAR_SLOT_DEFAULT_COLOR[oi + 1]), 0);
            lv_obj_set_style_bg_opa(pfill, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(pfill, 4, 0);
            lv_obj_set_style_border_width(pfill, 0, 0);
            lv_obj_set_style_pad_all(pfill, 0, 0);
            lv_obj_clear_flag(pfill, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            partner->fill = pfill;
            partner->bg = nullptr;  // shares the owner's track (no own bg/marker)
            partner->track_total = fill_len;
            partner->track_cross = track_cross;
            partner->cap = bar_chart_make_center_cap(
                pfill, cfg->horizontal, partner->dual_dir, track_cross,
                resolve_lv_color(bar_slot_color_cfg(cfg, oi + 1), BAR_SLOT_DEFAULT_COLOR[oi + 1]));
        }

        // Per-bar scale gridlines (evenly spaced across the fill direction).
        if (cfg->tick_count > 0 && st->tick_lines) {
            for (uint8_t t = 1; t <= cfg->tick_count; t++) {
                float ratio = (float)t / (cfg->tick_count + 1);
                int16_t px = (int16_t)roundf(ratio * fill_len);
                lv_obj_t* gl = lv_obj_create(bg);
                lv_obj_set_style_bg_color(gl, tick_clr, 0);
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

        // Per-bar target marker zone band + line (positioned later in tick).
        if (cfg->marker_value[0]) {
            if (cfg->marker_zone_pct > 0) {
                lv_obj_t* zone = lv_obj_create(bg);
                lv_obj_set_size(zone, 0, 0);
                lv_obj_set_style_bg_color(zone, resolve_lv_color(cfg->marker_zone_color, 0xFF5722), 0);
                lv_obj_set_style_bg_opa(zone, LV_OPA_50, 0);
                lv_obj_set_style_radius(zone, 0, 0);
                lv_obj_set_style_border_width(zone, 0, 0);
                lv_obj_set_style_pad_all(zone, 0, 0);
                lv_obj_clear_flag(zone, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
                slot->marker_zone = zone;
            }
            if (cfg->marker_width > 0) {
                lv_obj_t* line = lv_obj_create(bg);
                lv_obj_set_size(line, 0, 0);
                lv_obj_set_style_bg_color(line, resolve_lv_color(cfg->marker_color, 0xFFFFFF), 0);
                lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
                lv_obj_set_style_radius(line, 0, 0);
                lv_obj_set_style_border_width(line, 0, 0);
                lv_obj_set_style_pad_all(line, 0, 0);
                lv_obj_clear_flag(line, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
                slot->marker_line = line;
            }
        }

        // Per-bar near caption (gauge-style: bindable text, color-matched to the
        // bar, hidden when empty). Vertical: centered beneath the column.
        // Horizontal: right-aligned in the left strip, vertically centered on the row.
        const char* lbl_templ = bar_slot_label_cfg(cfg, oi);
        if (has_near_labels && lbl_templ[0]) {
            lv_obj_t* lbl = lv_label_create(tile);
            lv_obj_set_style_text_font(lbl, scale->font_small, 0);
            lv_obj_set_style_text_color(lbl, resolve_lv_color(bar_slot_color_cfg(cfg, oi), BAR_SLOT_DEFAULT_COLOR[oi]), 0);
            lv_obj_set_style_pad_all(lbl, 0, 0);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
            lv_obj_clear_flag(lbl, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
            bar_chart_set_label_text(lbl, lbl_templ);
            if (cfg->horizontal) {
                lv_obj_set_width(lbl, hlabel_w - 4);
                lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_RIGHT, 0);
                // Center the (possibly multi-line) caption block on the row.
                lv_obj_update_layout(lbl);
                int16_t lbl_h = (int16_t)lv_obj_get_height(lbl);
                int16_t row_center = row_top + track_cross / 2;
                lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, ui_ofs_x, row_center - lbl_h / 2 + ui_ofs_y);
            } else {
                lv_obj_set_width(lbl, col_cross);
                lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_align(lbl, LV_ALIGN_TOP_MID, ui_ofs_x + col_center_x, bar_top + fill_len + 4);
            }
            slot->label = lbl;
        }

        // Dual far-partner caption: right strip (horizontal, left-aligned) or
        // above the column (vertical, centered), color-matched to the far bar.
        if (partner) {
            const char* far_templ = bar_slot_label_cfg(cfg, oi + 1);
            if (far_templ[0]) {
                lv_obj_t* lbl = lv_label_create(tile);
                lv_obj_set_style_text_font(lbl, scale->font_small, 0);
                lv_obj_set_style_text_color(lbl, resolve_lv_color(bar_slot_color_cfg(cfg, oi + 1), BAR_SLOT_DEFAULT_COLOR[oi + 1]), 0);
                lv_obj_set_style_pad_all(lbl, 0, 0);
                lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
                lv_obj_clear_flag(lbl, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
                bar_chart_set_label_text(lbl, far_templ);
                if (cfg->horizontal) {
                    lv_obj_set_width(lbl, hlabel_w_far - 4);
                    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, 0);
                    lv_obj_update_layout(lbl);
                    int16_t lbl_h = (int16_t)lv_obj_get_height(lbl);
                    int16_t row_center = row_top + track_cross / 2;
                    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, ui_ofs_x + content_w - hlabel_w_far + 4, row_center - lbl_h / 2 + ui_ofs_y);
                } else {
                    lv_obj_set_width(lbl, col_cross);
                    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_align(lbl, LV_ALIGN_TOP_MID, ui_ofs_x + col_center_x, bar_top - vlabel_h_far);
                }
                partner->label = lbl;
            }
        }
    }

    st->has_received_data = false;
    st->horizontal = cfg->horizontal;
    st->zero_centered = cfg->zero_centered;
}

// ---- Animation exec callback for bar fill dimension (per slot) ----
static void bar_chart_anim_cb(void* var, int32_t value) {
    auto* slot = reinterpret_cast<BarSlot*>(var);
    BarChartState* st = slot->owner;
    if (!slot->fill) return;
    slot->last_anim_px = (int16_t)value;
    // Dual-binding halves are always zero-anchored, regardless of zero_centered.
    if (st->zero_centered || slot->dual_dir != 0) {
        // `value` is the value-edge position (px from the min/bottom end).
        // The opposite edge is pinned to the zero baseline.
        int32_t z = st->zero_px;
        int32_t lo = (value < z) ? value : z;
        int32_t hi = (value > z) ? value : z;
        int32_t size = hi - lo;
        // Single zero-centered bars flip which edge meets the baseline with the
        // sign of the value; dual halves keep their fixed center seam.
        if (slot->cap && slot->dual_dir == 0) {
            bar_chart_align_center_cap(slot->cap, st->horizontal, value >= z ? (int8_t)+1 : (int8_t)-1);
        }
        if (st->horizontal) {
            lv_obj_set_size(slot->fill, size, slot->track_cross);
            lv_obj_align(slot->fill, LV_ALIGN_TOP_LEFT, lo, 0);
        } else {
            lv_obj_set_size(slot->fill, slot->track_cross, size);
            lv_obj_align(slot->fill, LV_ALIGN_BOTTOM_LEFT, 0, -lo);
        }
    } else if (st->horizontal) {
        lv_obj_set_size(slot->fill, value, slot->track_cross);
        lv_obj_align(slot->fill, LV_ALIGN_TOP_LEFT, 0, 0);
    } else {
        lv_obj_set_size(slot->fill, slot->track_cross, value);
        lv_obj_align(slot->fill, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
}

// ---- Helper: position one bar's target marker line + zone band ----
static void bar_chart_position_marker(BarSlot* slot, const BarChartConfig* cfg,
                                      float mn, float mx, float marker_val, bool horizontal) {
    if (!slot->marker_line && !slot->marker_zone) return;
    float range = mx - mn;
    if (range <= 0.0f) range = 1.0f;
    float ratio = (marker_val - mn) / range;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    const int16_t track_total = slot->track_total;
    const int16_t track_cross = slot->track_cross;
    const int16_t px = (int16_t)roundf(ratio * track_total);

    // Zone band centered on the marker
    if (slot->marker_zone && cfg->marker_zone_pct > 0) {
        float half = (cfg->marker_zone_pct / 100.0f) / 2.0f;
        float lo_r = ratio - half;
        float hi_r = ratio + half;
        if (lo_r < 0.0f) lo_r = 0.0f;
        if (hi_r > 1.0f) hi_r = 1.0f;
        int16_t lo_px = (int16_t)roundf(lo_r * track_total);
        int16_t hi_px = (int16_t)roundf(hi_r * track_total);
        int16_t band = hi_px - lo_px;
        if (band < 1) band = 1;
        if (horizontal) {
            lv_obj_set_size(slot->marker_zone, band, track_cross);
            lv_obj_align(slot->marker_zone, LV_ALIGN_LEFT_MID, lo_px, 0);
        } else {
            lv_obj_set_size(slot->marker_zone, track_cross, band);
            lv_obj_align(slot->marker_zone, LV_ALIGN_BOTTOM_MID, 0, -lo_px);
        }
    }

    // Marker line centered on the marker position
    if (slot->marker_line && cfg->marker_width > 0) {
        if (horizontal) {
            lv_obj_set_size(slot->marker_line, cfg->marker_width, track_cross);
            lv_obj_align(slot->marker_line, LV_ALIGN_LEFT_MID, px - cfg->marker_width / 2, 0);
        } else {
            lv_obj_set_size(slot->marker_line, track_cross, cfg->marker_width);
            lv_obj_align(slot->marker_line, LV_ALIGN_BOTTOM_MID, 0, cfg->marker_width / 2 - px);
        }
    }
}

static void bar_chart_update(lv_obj_t* tile, const WidgetConfig* wcfg,
                              WidgetState* state, const char* raw_value) {
    auto* cfg = reinterpret_cast<const BarChartConfig*>(wcfg->data);
    auto* st = reinterpret_cast<BarChartState*>(state->data);

    if (!st->slots[0].fill || !st->slots[0].bg) return;

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

    // Resolve bindable min/max (widget-wide). Do NOT write cached_min/cached_max
    // here — bar_chart_tick() owns range-change detection (marker reposition +
    // fill-rescale invalidation) and relies on the cache lagging the live value.
    float bar_max = resolve_number(cfg->bar_max, 3.0f);
    float bar_min = resolve_number(cfg->bar_min, 0.0f);

    float range = bar_max - bar_min;
    if (range <= 0.0f) range = 1.0f;

    // All bars share the same track length along the fill direction.
    int16_t bar_total = st->slots[0].track_total;

    // Zero baseline (widget-wide; same min/max for all bars). Also computed when a
    // dual pair is active, since both dual halves anchor here.
    if (cfg->zero_centered || st->dual_pair_1_active || st->dual_pair_2_active) {
        float zero_ratio = (0.0f - bar_min) / range;
        if (zero_ratio < 0.0f) zero_ratio = 0.0f;
        if (zero_ratio > 1.0f) zero_ratio = 1.0f;
        st->zero_px = (int16_t)roundf(zero_ratio * bar_total);
    }

    // Animation timing decision (shared across all bars this cycle)
    uint32_t now = lv_tick_get();
    bool rapid_update = st->has_received_data &&
                        (now - st->last_update_ms) < (uint32_t)cfg->anim_ms;
    st->last_update_ms = now;
    bool had_data = st->has_received_data;
    st->has_received_data = true;

    for (uint8_t s = 0; s < st->num_bars; s++) {
        BarSlot* slot = &st->slots[s];
        if (!slot->fill) continue;

        char* end = nullptr;
        float value = strtof(vals[s], &end);
        if (end == vals[s]) continue; // Not a number — leave this bar untouched

        // Skip redundant updates (within 0.001 tolerance)
        if (!isnan(slot->last_value) && fabsf(value - slot->last_value) < 0.001f) continue;
        slot->last_value = value;

        // Dual halves are zero-anchored: the far half (dual_dir +1) grows toward
        // max, the near half (dual_dir -1) flips its magnitude toward min.
        bool zc = cfg->zero_centered || slot->dual_dir != 0;
        float eff = (slot->dual_dir != 0) ? slot->dual_dir * value : value;

        // `target_px` is the animated quantity: in normal mode the fill
        // dimension; in zero-anchored mode the value-edge position.
        int16_t target_px;
        if (zc) {
            float val_ratio = (eff - bar_min) / range;
            if (val_ratio < 0.0f) val_ratio = 0.0f;
            if (val_ratio > 1.0f) val_ratio = 1.0f;
            target_px = (int16_t)roundf(val_ratio * bar_total);
        } else {
            float ratio = (value - bar_min) / range;
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            target_px = (int16_t)(ratio * bar_total);
            if (target_px == 0 && value > bar_min) target_px = 1;
        }

        int16_t cur_px = slot->last_anim_px;
        bool should_animate = had_data && cfg->anim_ms > 0
                              && cur_px != target_px && !rapid_update;
        if (should_animate) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, slot);
            lv_anim_set_values(&a, cur_px, target_px);
            lv_anim_set_duration(&a, cfg->anim_ms);
            lv_anim_set_exec_cb(&a, bar_chart_anim_cb);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_start(&a);
        } else {
            bar_chart_anim_cb(slot, target_px);
        }

        // Apply bar fill color (shared cache with tick)
        lv_color_t clr;
        if (resolve_color_changed(bar_slot_color_cfg(cfg, s), BAR_SLOT_DEFAULT_COLOR[s],
                                  &slot->cached_color, &clr))
            lv_obj_set_style_bg_color(slot->fill, clr, 0);
    }
}

// ---- Tick: re-resolve binding-driven colors (skip if unchanged) ----

static void bar_chart_tick(lv_obj_t* tile, const WidgetConfig* wcfg,
                           WidgetState* state) {
    auto* cfg = reinterpret_cast<const BarChartConfig*>(wcfg->data);
    auto* st = reinterpret_cast<BarChartState*>(state->data);
    if (!st->slots[0].fill || !st->slots[0].bg) return;

    // Re-resolve bindable min/max; invalidate slot values so next update re-applies
    float mn = resolve_number(cfg->bar_min, 0.0f);
    float mx = resolve_number(cfg->bar_max, 3.0f);
    bool range_changed = (mn != st->cached_min || mx != st->cached_max);
    if (range_changed) {
        st->cached_min = mn;
        st->cached_max = mx;
        for (uint8_t s = 0; s < st->num_bars; s++) st->slots[s].last_value = NAN;
    }

    lv_color_t clr;
    if (resolve_color_changed(cfg->bar_bg_color, 0x1A1A1A, &st->cached_bar_bg_color, &clr))
        for (uint8_t s = 0; s < st->num_bars; s++)
            if (st->slots[s].bg) lv_obj_set_style_bg_color(st->slots[s].bg, clr, 0);
    for (uint8_t s = 0; s < st->num_bars; s++) {
        BarSlot* slot = &st->slots[s];
        if (slot->fill &&
            resolve_color_changed(bar_slot_color_cfg(cfg, s), BAR_SLOT_DEFAULT_COLOR[s],
                                  &slot->cached_color, &clr)) {
            lv_obj_set_style_bg_color(slot->fill, clr, 0);
            if (slot->cap) lv_obj_set_style_bg_color(slot->cap, clr, 0);
            if (slot->label) lv_obj_set_style_text_color(slot->label, clr, 0);
        }
    }

    // Re-resolve per-bar captions (binding-driven text updates live; static
    // captions resolve to the same string and skip the relayout).
    for (uint8_t s = 0; s < st->num_bars; s++) {
        if (st->slots[s].label)
            bar_chart_set_label_text(st->slots[s].label, bar_slot_label_cfg(cfg, s));
    }

    // Gridline color (uses cached pointers — no child scan)
    if (st->tick_lines && st->tick_line_count > 0) {
        if (resolve_color_changed(cfg->tick_color, 0x808080, &st->cached_tick_color, &clr)) {
            for (uint8_t i = 0; i < st->tick_line_count; i++)
                lv_obj_set_style_bg_color(st->tick_lines[i], clr, 0);
        }
    }

    // Target marker position + color (each bar carries its own marker objects)
    if (cfg->marker_value[0]) {
        float marker_val = resolve_number(cfg->marker_value, NAN);
        bool val_changed = isnan(st->cached_marker_val) || fabsf(marker_val - st->cached_marker_val) > 0.001f;
        if (!isnan(marker_val) && (val_changed || range_changed)) {
            st->cached_marker_val = marker_val;
            for (uint8_t s = 0; s < st->num_bars; s++)
                bar_chart_position_marker(&st->slots[s], cfg, mn, mx, marker_val, st->horizontal);
        }
        if (resolve_color_changed(cfg->marker_color, 0xFFFFFF, &st->cached_marker_color, &clr))
            for (uint8_t s = 0; s < st->num_bars; s++)
                if (st->slots[s].marker_line) lv_obj_set_style_bg_color(st->slots[s].marker_line, clr, 0);
        if (resolve_color_changed(cfg->marker_zone_color, 0xFF5722, &st->cached_marker_zone_color, &clr))
            for (uint8_t s = 0; s < st->num_bars; s++)
                if (st->slots[s].marker_zone) lv_obj_set_style_bg_color(st->slots[s].marker_zone, clr, 0);
    }
}

static void bar_chart_destroy(WidgetState* state) {
    auto* st = reinterpret_cast<BarChartState*>(state->data);
    // Null all fills + tracks so any in-flight animation callbacks bail out.
    for (uint8_t s = 0; s < MAX_WIDGET_BINDINGS; s++) {
        st->slots[s].fill = nullptr;
        st->slots[s].bg = nullptr;
        st->slots[s].label = nullptr;
        st->slots[s].cap = nullptr;
    }
    // Free the heap-allocated gridline pointer array (the LVGL objects
    // themselves are deleted automatically with the parent tile).
    if (st->tick_lines) {
        lv_free(st->tick_lines);
        st->tick_lines = nullptr;
    }
}

// ---- Registration ----

#if HAS_MCP
static void bar_chart_describe(JsonObject& out) {
    JsonArray f = out.createNestedArray("config_fields");
    auto add = [&](const char* n, const char* t, const char* d){ JsonObject o=f.createNestedObject(); o["name"]=n; o["type"]=t; o["desc"]=d; };
    auto addmax = [&](const char* n, const char* t, const char* d, int m){ JsonObject o=f.createNestedObject(); o["name"]=n; o["type"]=t; o["desc"]=d; o["max"]=m; };
    add("widget_bar_min","number","scale minimum (bindable)"); add("widget_bar_max","number","full-scale value (bindable)");
    add("widget_orientation","string","'horizontal' or 'vertical' (default vertical)");
    add("widget_bar_width_pct","number","bar thickness 1-100% of its column"); add("widget_bar_zero_centered","bool","fill from 0 baseline instead of min");
    add("widget_bar_bg_color","color","track/background color");
    add("widget_bar_color","color","bar 1 fill"); add("widget_bar_color_2","color","bar 2 fill (2nd binding)");
    add("widget_bar_color_3","color","bar 3 fill"); add("widget_bar_color_4","color","bar 4 fill");
    addmax("widget_bar_label","string","bar 1 caption",63); addmax("widget_bar_label_2","string","bar 2 caption",63);
    addmax("widget_bar_label_3","string","bar 3 caption",63); addmax("widget_bar_label_4","string","bar 4 caption",63);
    add("widget_bar_label_size","number","caption strip px, 0=auto");
    add("widget_bar_dual_binding_pair_1","bool","bind 1&2 share one center bar: 1 grows down/left, 2 up/right (need min<0)");
    add("widget_bar_dual_binding_pair_2","bool","same for bindings 3&4");
    add("widget_bar_ticks","number","gridline count"); add("widget_bar_tick_color","color","gridline color"); add("widget_bar_tick_width","number","gridline px 1-5");
    add("widget_bar_marker_value","number","target marker on scale ('' off)"); add("widget_bar_marker_color","color","marker line");
    add("widget_bar_marker_zone_color","color","zone band"); add("widget_bar_marker_zone_pct","number","zone width %"); add("widget_bar_marker_width","number","marker line px");
    add("widget_anim_ms","number","transition ms, 0=instant");
}
#endif
REGISTER_WIDGET_SCHEMA(bar_chart, nullptr, false);

#endif // HAS_DISPLAY
