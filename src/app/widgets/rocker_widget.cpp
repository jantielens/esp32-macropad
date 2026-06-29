#include "widget.h"

#if HAS_DISPLAY

#include "../log_manager.h"
#include <string.h>

#define TAG "Rocker"

// ============================================================================
// Rocker Widget
// ============================================================================
// Transforms a button into a directional rocker: tapping the top/bottom
// (or left/right) half dispatches the tap vs long-press action set.
// Visual cue: small chevron triangles at opposite edges.
//
// Layout within the tile (vertical mode):
//   ┌────────────────┐
//   │       ▲        │  ← chevron at top edge
//   │   label_top    │
//   │  label_center  │
//   │  label_bottom  │
//   │       ▼        │  ← chevron at bottom edge
//   └────────────────┘
//
// Horizontal mode: ◄ at left edge, ► at right edge.

// ---- Config struct (packed into WidgetConfig.data[]) ----

struct RockerConfig {
    bool horizontal;          // false = up/down (default), true = left/right
    char indicator_color[CONFIG_COLOR_MAX_LEN]; // Chevron color (default "#FFFFFF")
    uint8_t indicator_opa;    // Chevron opacity 0-255 (default 80 = ~31%)
};

static_assert(sizeof(RockerConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "RockerConfig exceeds WIDGET_CONFIG_MAX_BYTES");

// ---- Runtime state (packed into WidgetState.data[]) ----

struct RockerState {
    lv_obj_t* chevron_a;   // Top/left chevron label
    lv_obj_t* chevron_b;   // Bottom/right chevron label
};

static_assert(sizeof(RockerState) <= WIDGET_STATE_MAX_BYTES,
              "RockerState exceeds WIDGET_STATE_MAX_BYTES");

// ---- WidgetType callbacks ----

static void rocker_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<RockerConfig*>(data);
    memset(cfg, 0, sizeof(RockerConfig));

    const char* axis = btn["widget_rocker_axis"] | "";
    cfg->horizontal = (axis[0] == 'h' || axis[0] == 'H');

    widget_parse_field(btn["widget_rocker_color"], cfg->indicator_color,
                       sizeof(cfg->indicator_color), "#FFFFFF");

    int opa = btn["widget_rocker_opacity"] | 80;
    cfg->indicator_opa = (uint8_t)clamp_val(opa, 0, 255);
}

static void rocker_create(lv_obj_t* tile, const WidgetConfig* wcfg,
                           const ScreenButtonConfig* btn,
                           const PadRect* rect, const UIScaleInfo* scale,
                           lv_obj_t* icon_img, lv_obj_t* center_label,
                           WidgetState* state) {
    (void)center_label;
    auto* cfg = reinterpret_cast<const RockerConfig*>(wcfg->data);
    auto* st = reinterpret_cast<RockerState*>(state->data);
    memset(st, 0, sizeof(RockerState));

    // Resolve indicator color
    uint32_t rgb = 0xFFFFFF;
    parse_hex_color(cfg->indicator_color, &rgb);
    lv_color_t color = lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);

    // Choose chevron symbols and alignment based on axis
    const char* sym_a = cfg->horizontal ? LV_SYMBOL_LEFT  : LV_SYMBOL_UP;
    const char* sym_b = cfg->horizontal ? LV_SYMBOL_RIGHT : LV_SYMBOL_DOWN;

    // Chevron A (top or left)
    lv_obj_t* chev_a = lv_label_create(tile);
    lv_label_set_text(chev_a, sym_a);
    lv_obj_set_style_text_color(chev_a, color, 0);
    lv_obj_set_style_text_opa(chev_a, cfg->indicator_opa, 0);
    lv_obj_set_style_text_font(chev_a, scale->font_small, 0);
    if (cfg->horizontal) {
        lv_obj_align(chev_a, LV_ALIGN_LEFT_MID, 2, btn->ui_offset_y);
    } else {
        lv_obj_align(chev_a, LV_ALIGN_TOP_MID, btn->ui_offset_x, 1);
    }
    lv_obj_clear_flag(chev_a, LV_OBJ_FLAG_CLICKABLE);

    // Chevron B (bottom or right)
    lv_obj_t* chev_b = lv_label_create(tile);
    lv_label_set_text(chev_b, sym_b);
    lv_obj_set_style_text_color(chev_b, color, 0);
    lv_obj_set_style_text_opa(chev_b, cfg->indicator_opa, 0);
    lv_obj_set_style_text_font(chev_b, scale->font_small, 0);
    if (cfg->horizontal) {
        lv_obj_align(chev_b, LV_ALIGN_RIGHT_MID, -2, btn->ui_offset_y);
    } else {
        lv_obj_align(chev_b, LV_ALIGN_BOTTOM_MID, btn->ui_offset_x, -1);
    }
    lv_obj_clear_flag(chev_b, LV_OBJ_FLAG_CLICKABLE);

    st->chevron_a = chev_a;
    st->chevron_b = chev_b;

    LOGD(TAG, "Created rocker widget: %s mode, opa=%d",
         cfg->horizontal ? "horizontal" : "vertical", cfg->indicator_opa);
}

static void rocker_update(lv_obj_t* tile, const WidgetConfig* wcfg,
                           WidgetState* state, const char* raw_value) {
    // Rocker has no data binding — nothing to update
    (void)tile; (void)wcfg; (void)state; (void)raw_value;
}

static void rocker_tick(lv_obj_t* tile, const WidgetConfig* wcfg,
                         WidgetState* state) {
    // No periodic work needed
    (void)tile; (void)wcfg; (void)state;
}

static void rocker_destroy(WidgetState* state) {
    // LVGL children are deleted automatically with the tile
    (void)state;
}

// ---- Registration ----

#if HAS_MCP
static void rocker_describe(JsonObject& out) {
    JsonArray f = out.createNestedArray("config_fields");
    auto add = [&](const char* n, const char* t, const char* d){ JsonObject o=f.createNestedObject(); o["name"]=n; o["type"]=t; o["desc"]=d; };
    add("widget_rocker_axis","string","'h' or 'v' rocker direction");
    add("widget_rocker_color","color","rocker color"); add("widget_rocker_opacity","number","0-100");
}
#endif
REGISTER_WIDGET_SCHEMA(rocker, nullptr, false);

#endif // HAS_DISPLAY
