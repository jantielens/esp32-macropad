#include "widget.h"

#if HAS_DISPLAY

#include "../log_manager.h"
#include "../action_parse.h"
#include "numericrocker_zones.h"
#include <string.h>

#define TAG "NRocker"

// ============================================================================
// Numeric Rocker Widget
// ============================================================================
// 4-zone directional control with fine and coarse adjustment:
//   Horizontal: «  ‹  [label]  ›  »
//   Vertical:   ▲▲  ▲  [label]  ▼  ▼▼
//
// Each zone dispatches the same action template with {step} replaced by
// ±small_step (inner) or ±large_step (outer).
//
// Zone layout: pixel-clamped zones that adapt to button size.
// Each outer/inner zone targets a percentage of the span but is clamped
// to a min/max pixel range. The center (label) gets whatever remains.

// ---- Config struct — defined in numericrocker_zones.h ----

static_assert(sizeof(NumericRockerConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "NumericRockerConfig exceeds WIDGET_CONFIG_MAX_BYTES");

// ---- Runtime state (packed into WidgetState.data[]) ----

struct NumericRockerState {
    lv_obj_t* chevron_outer_a;   // Outer dec chevron (left/top)
    lv_obj_t* chevron_inner_a;   // Inner dec chevron
    lv_obj_t* chevron_inner_b;   // Inner inc chevron
    lv_obj_t* chevron_outer_b;   // Outer inc chevron (right/bottom)
};

static_assert(sizeof(NumericRockerState) <= WIDGET_STATE_MAX_BYTES,
              "NumericRockerState exceeds WIDGET_STATE_MAX_BYTES");

// ---- WidgetType callbacks ----

static void numericrocker_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<NumericRockerConfig*>(data);
    memset(cfg, 0, sizeof(NumericRockerConfig));

    const char* axis = btn["widget_numericrocker_axis"] | "horizontal";
    cfg->horizontal = (axis[0] != 'v' && axis[0] != 'V');

    cfg->small_step = btn["widget_numericrocker_small_step"] | 1.0f;
    if (cfg->small_step < 0) cfg->small_step = 0;

    cfg->large_step = btn["widget_numericrocker_large_step"] | 10.0f;
    if (cfg->large_step < 0) cfg->large_step = 0;

    widget_parse_field(btn["widget_numericrocker_color"], cfg->indicator_color,
                       sizeof(cfg->indicator_color), "#FFFFFF");

    int opa = btn["widget_numericrocker_opacity"] | 80;
    cfg->indicator_opa = (uint8_t)clamp_val(opa, 0, 255);

    // Parse the adjustment action (nested object)
    memset(&cfg->adjust_action, 0, sizeof(ButtonAction));
    JsonObject adj = btn["widget_numericrocker_action"];
    if (!adj.isNull()) {
        action_parse(adj, cfg->adjust_action);
    }
}

static void numericrocker_create(lv_obj_t* tile, const WidgetConfig* wcfg,
                                  const ScreenButtonConfig* btn,
                                  const PadRect* rect, const UIScaleInfo* scale,
                                  lv_obj_t* icon_img, lv_obj_t* center_label,
                                  WidgetState* state) {
    (void)center_label;
    auto* cfg = reinterpret_cast<const NumericRockerConfig*>(wcfg->data);
    auto* st = reinterpret_cast<NumericRockerState*>(state->data);
    memset(st, 0, sizeof(NumericRockerState));

    // Resolve indicator color
    uint32_t rgb = 0xFFFFFF;
    parse_hex_color(cfg->indicator_color, &rgb);
    lv_color_t color = lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);

    // Choose symbols based on axis
    const char* sym_outer_a;
    const char* sym_inner_a;
    const char* sym_inner_b;
    const char* sym_outer_b;

    if (cfg->horizontal) {
        sym_outer_a = "<<";
        sym_inner_a = "<";
        sym_inner_b = ">";
        sym_outer_b = ">>";
    } else {
        sym_outer_a = LV_SYMBOL_UP LV_SYMBOL_UP;
        sym_inner_a = LV_SYMBOL_UP;
        sym_inner_b = LV_SYMBOL_DOWN;
        sym_outer_b = LV_SYMBOL_DOWN LV_SYMBOL_DOWN;
    }

    // Helper: create a chevron label with common styling
    auto make_chevron = [&](const char* sym) -> lv_obj_t* {
        lv_obj_t* lbl = lv_label_create(tile);
        lv_label_set_text(lbl, sym);
        lv_obj_set_style_text_color(lbl, color, 0);
        lv_obj_set_style_text_opa(lbl, cfg->indicator_opa, 0);
        lv_obj_set_style_text_font(lbl, scale->font_medium, 0);
        lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        return lbl;
    };

    // Compute pixel-clamped zone boundaries
    int span = cfg->horizontal ? rect->w : rect->h;
    NRZoneLayout z = nr_compute_zones(span, cfg->small_step, cfg->large_step);

    // Create chevrons only for active zones.
    // Use LV_ALIGN_CENTER so LVGL centers the label's midpoint at the offset.
    // Offset = zone_center_px - span/2  (distance from tile center).
    int half = span / 2;

    auto place_chevron = [&](const char* sym, int zone_start, int zone_end, bool is_horizontal) -> lv_obj_t* {
        if (zone_start >= zone_end) return nullptr; // disabled zone
        lv_obj_t* lbl = make_chevron(sym);
        int center = (zone_start + zone_end) / 2;
        if (is_horizontal) {
            lv_obj_align(lbl, LV_ALIGN_CENTER, center - half, btn->ui_offset_y);
        } else {
            lv_obj_align(lbl, LV_ALIGN_CENTER, btn->ui_offset_x, center - half);
        }
        return lbl;
    };

    bool horiz = cfg->horizontal;
    st->chevron_outer_a = place_chevron(sym_outer_a, 0, z.outer_end, horiz);
    st->chevron_inner_a = place_chevron(sym_inner_a, z.outer_end, z.inner_end, horiz);
    st->chevron_inner_b = place_chevron(sym_inner_b, z.inner2_start, z.outer2_start, horiz);
    st->chevron_outer_b = place_chevron(sym_outer_b, z.outer2_start, span, horiz);

    LOGD(TAG, "Created numeric rocker: %s, small=%g, large=%g, opa=%d",
         cfg->horizontal ? "horizontal" : "vertical",
         cfg->small_step, cfg->large_step, cfg->indicator_opa);
}

static void numericrocker_update(lv_obj_t* tile, const WidgetConfig* wcfg,
                                  WidgetState* state, const char* raw_value) {
    (void)tile; (void)wcfg; (void)state; (void)raw_value;
}

static void numericrocker_tick(lv_obj_t* tile, const WidgetConfig* wcfg,
                                WidgetState* state) {
    (void)tile; (void)wcfg; (void)state;
}

static void numericrocker_destroy(WidgetState* state) {
    (void)state;
}

// ---- {step} substitution helpers (used by pad_screen_events.cpp) ----

// Replace all occurrences of "{step}" in a char buffer with the signed value.
static void substitute_step_in_field(char* field, size_t field_size, float step) {
    const char* token = "{step}";
    const size_t token_len = 6;
    char tmp[24];
    snprintf(tmp, sizeof(tmp), "%g", step);
    size_t repl_len = strlen(tmp);

    char* pos = strstr(field, token);
    while (pos) {
        size_t tail_len = strlen(pos + token_len);
        if ((size_t)(pos - field) + repl_len + tail_len >= field_size) return;
        memmove(pos + repl_len, pos + token_len, tail_len + 1);
        memcpy(pos, tmp, repl_len);
        pos = strstr(pos + repl_len, token);
    }
}

void numericrocker_substitute_step(ButtonAction* act, float step) {
    substitute_step_in_field(act->mqtt_payload, sizeof(act->mqtt_payload), step);
    substitute_step_in_field(act->key_sequence, sizeof(act->key_sequence), step);
    substitute_step_in_field(act->volume_value, sizeof(act->volume_value), step);
    substitute_step_in_field(act->brightness_value, sizeof(act->brightness_value), step);
    substitute_step_in_field(act->timer_value, sizeof(act->timer_value), step);
}

// ---- Registration ----

REGISTER_WIDGET(numericrocker, nullptr, false);

#endif // HAS_DISPLAY
