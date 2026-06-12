#include "widget.h"

#if HAS_DISPLAY

#include "../log_manager.h"
#include "../action_parse.h"
#include "../action_registry.h"
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

void numericrocker_substitute_step(ButtonAction* act, float step) {
    // Type-dispatched: only touch the active union arm.
    if (strcmp(act->type, ACTION_TYPE_MQTT) == 0) {
        action_substitute_step_field(act->payload.mqtt.mqtt_payload,
                                     sizeof(act->payload.mqtt.mqtt_payload), step);
    } else if (strcmp(act->type, ACTION_TYPE_KEY) == 0) {
        action_substitute_step_field(act->payload.key.key_sequence,
                                     sizeof(act->payload.key.key_sequence), step);
    } else if (strcmp(act->type, ACTION_TYPE_VOLUME) == 0) {
        action_substitute_step_field(act->payload.volume.volume_value,
                                     sizeof(act->payload.volume.volume_value), step);
    } else if (strcmp(act->type, ACTION_TYPE_BRIGHTNESS) == 0) {
        action_substitute_step_field(act->payload.brightness.brightness_value,
                                     sizeof(act->payload.brightness.brightness_value), step);
    } else if (strcmp(act->type, ACTION_TYPE_TIMER) == 0) {
        action_substitute_step_field(act->payload.timer.timer_value,
                                     sizeof(act->payload.timer.timer_value), step);
    } else {
        // Device-class action types store their value in the opaque
        // payload arm; substitute {step} generically against the registered
        // value_field accessor so it reaches fields shared code cannot see
        // (e.g. darkroom strip/expose/meter).
        const ActionTypeDef* def = action_type_find(act->type);
        action_type_substitute_step(def, *act, step);
    }
}

// ---- Registration ----

REGISTER_WIDGET(numericrocker, nullptr, false);

#endif // HAS_DISPLAY
