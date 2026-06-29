#include "widget.h"

#if HAS_DISPLAY

#include "../screens/pad_screen.h"
#include "../list_provider.h"
#include "../list_binding.h"
#include "../action_dispatch.h"
#include <string.h>
#include <stddef.h>

// ============================================================================
// List Widget — scrollable tappable list from a ListProvider
// ============================================================================
// Dispatches the button's normal tap/long-press actions on item interaction.
// The widget sets [list:provider_id.selected] binding before dispatch so action
// fields containing that token resolve to the tapped item's ID.
// ============================================================================

struct ListWidgetState {
    ButtonTile* tile;                    // parent tile (for action dispatch)
    const char* provider_id;             // points into tile->widget_cfg.data_binding[0]
    const lv_font_t* item_font;          // resolved center label font
    lv_color_t item_color;               // resolved center label color
    lv_coord_t item_radius;              // resolved from button corner_radius
    char (*ids)[LIST_ITEM_ID_MAX];       // heap-allocated array of item ID strings
    uint8_t count;
};

static_assert(sizeof(ListWidgetState) <= WIDGET_STATE_MAX_BYTES,
              "ListWidgetState exceeds WIDGET_STATE_MAX_BYTES");

// ---- Dispatch helper (mirrors pad_screen_events onTap/onLongPress pattern) ----

static void list_dispatch_actions(ListWidgetState* st, uintptr_t idx,
                                   const ButtonAction* src, uint8_t count,
                                   const char* label) {
    if (!count) return;
    list_binding_set_selected(st->provider_id, st->ids[idx]);
    ButtonAction local[MAX_BUTTON_ACTIONS];
    memcpy(local, src, count * sizeof(ButtonAction));
    for (uint8_t i = 0; i < count; i++) {
        action_dispatch(local[i], label);
    }
}

// ---- Event handlers ----

static void list_item_clicked(lv_event_t* e) {
    auto* st = reinterpret_cast<ListWidgetState*>(lv_event_get_user_data(e));
    if (!st || !st->tile) return;
    if (!st->tile->action_count) return;

    lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
    uintptr_t idx = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target));
    if (idx >= st->count) return;

    list_dispatch_actions(st, idx, st->tile->actions, st->tile->action_count, "ListSelect");
}

static void list_item_long_pressed(lv_event_t* e) {
    auto* st = reinterpret_cast<ListWidgetState*>(lv_event_get_user_data(e));
    if (!st || !st->tile) return;
    if (!st->tile->lp_action_count) return;

    lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
    uintptr_t idx = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target));
    if (idx >= st->count) return;

    list_dispatch_actions(st, idx, st->tile->lp_actions, st->tile->lp_action_count, "ListLongPress");
}

// ---- WidgetType callbacks ----

static void list_parse(const JsonObject& btn, uint8_t* data) {
    (void)btn; (void)data;
    // No widget-specific config — actions come from the button's tap/lp arrays
}

static void list_create(lv_obj_t* tile, const WidgetConfig* wcfg,
                         const ScreenButtonConfig* btn,
                         const PadRect* rect, const UIScaleInfo* scale,
                         lv_obj_t* icon_img, lv_obj_t* center_label,
                         WidgetState* state) {
    auto* st = reinterpret_cast<ListWidgetState*>(state->data);
    memset(st, 0, sizeof(ListWidgetState));

    // Derive ButtonTile* from our WidgetState* (which is embedded in ButtonTile)
    st->tile = reinterpret_cast<ButtonTile*>(
        reinterpret_cast<uint8_t*>(state) - offsetof(ButtonTile, widget_state));
    st->provider_id = wcfg->data_binding[0];

    // Resolve center label styling from button config (btn is only valid during createUI)
    uint32_t fg_raw = 0xFFFFFF;
    parse_hex_color(btn->fg_color, &fg_raw);
    lv_color_t fg = lv_color_hex(fg_raw);
    st->item_font = pad_resolve_font(btn->style_center, scale->font_large);
    st->item_color = pad_resolve_label_color(btn->style_center, fg);
    st->item_radius = (lv_coord_t)strtol(btn->corner_radius, nullptr, 10);

    // Hide standard labels — widget controls the full tile
    if (center_label) lv_obj_add_flag(center_label, LV_OBJ_FLAG_HIDDEN);
    if (icon_img) lv_obj_add_flag(icon_img, LV_OBJ_FLAG_HIDDEN);

    // Look up provider from data binding
    const char* provider_id = wcfg->data_binding[0];
    const ListProvider* provider = list_provider_find(provider_id);

    if (!provider) {
        lv_obj_t* lbl = lv_label_create(tile);
        lv_label_set_text(lbl, "Source not found");
        lv_obj_center(lbl);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(lbl, scale->font_small, 0);
        return;
    }

    // Get items from provider
    ListItem items[LIST_MAX_ITEMS];
    uint8_t count = provider->provide(items, LIST_MAX_ITEMS);

    // Apply optional filter from data_binding[1]
    const char* filter = wcfg->data_binding[1];
    if (filter[0] != '\0') {
        count = list_filter_items(items, count, filter);
    }

    if (count == 0) {
        lv_obj_t* lbl = lv_label_create(tile);
        lv_label_set_text(lbl, "No items");
        lv_obj_center(lbl);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(lbl, scale->font_small, 0);
        return;
    }

    // Store item IDs for click handler
    st->ids = reinterpret_cast<char(*)[LIST_ITEM_ID_MAX]>(malloc(count * LIST_ITEM_ID_MAX));
    if (!st->ids) return;
    st->count = count;
    for (uint8_t i = 0; i < count; i++) {
        memcpy(st->ids[i], items[i].id, LIST_ITEM_ID_MAX);
    }

    // Create scrollable container (no flex/list — LV_USE_FLEX and LV_USE_LIST disabled).
    // Inset vertically so top/bottom button labels remain visible above/below the list.
    // Account for the tile's pad_all (TILE_PAD_PX=4 in pad_screen.cpp) since labels
    // and the container are positioned within the tile's content area.
    const lv_coord_t TILE_PAD = 4;
    const lv_font_t* top_font = pad_resolve_font(btn->style_top, scale->font_small);
    const lv_font_t* bot_font = pad_resolve_font(btn->style_bottom, scale->font_small);
    const lv_coord_t top_h = btn->label_top[0] ? lv_font_get_line_height(top_font) : 0;
    const lv_coord_t bot_h = btn->label_bottom[0] ? lv_font_get_line_height(bot_font) : 0;
    const lv_coord_t cont_h = rect->h - 2 * TILE_PAD - top_h - bot_h;

    lv_obj_t* cont = lv_obj_create(tile);
    lv_obj_set_size(cont, rect->w, cont_h);
    lv_obj_align(cont, LV_ALIGN_CENTER, 0, (top_h - bot_h) / 2);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 2, 0);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLL_ELASTIC);
    // Match scrollbar color to item text color
    lv_obj_set_style_bg_color(cont, st->item_color, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(cont, LV_OPA_70, LV_PART_SCROLLBAR);

    const lv_coord_t item_pad_v = 4;
    const lv_coord_t item_h = lv_font_get_line_height(st->item_font) + item_pad_v * 2;
    const lv_coord_t gap = 2;
    const lv_coord_t content_w = rect->w - 4; // account for pad_all=2

    for (uint8_t i = 0; i < count; i++) {
        lv_obj_t* item = lv_obj_create(cont);
        lv_obj_set_size(item, content_w, item_h);
        lv_obj_set_pos(item, 0, i * (item_h + gap));
        lv_obj_set_user_data(item, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(item, list_item_clicked, LV_EVENT_SHORT_CLICKED, st);
        lv_obj_add_event_cb(item, list_item_long_pressed, LV_EVENT_LONG_PRESSED, st);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_style_bg_opa(item, LV_OPA_50, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x444444), 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x666666), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, st->item_radius, 0);
        lv_obj_set_style_pad_ver(item, item_pad_v, 0);
        lv_obj_set_style_pad_hor(item, 8, 0);

        lv_obj_t* label = lv_label_create(item);
        lv_label_set_text(label, items[i].label);
        lv_obj_set_style_text_color(label, st->item_color, 0);
        lv_obj_set_style_text_font(label, st->item_font, 0);
        lv_obj_center(label);
    }
}

static void list_update(lv_obj_t* tile, const WidgetConfig* cfg,
                         WidgetState* state, const char* raw_value) {
    (void)tile; (void)cfg; (void)state; (void)raw_value;
}

static void list_tick(lv_obj_t* tile, const WidgetConfig* cfg, WidgetState* state) {
    (void)tile; (void)cfg; (void)state;
}

static void list_destroy(WidgetState* state) {
    auto* st = reinterpret_cast<ListWidgetState*>(state->data);
    if (st->ids) {
        free(st->ids);
        st->ids = nullptr;
    }
}

// ---- Registration ----

#if HAS_MCP
static void list_describe(JsonObject& out) {
    // No button-level config knobs: the list's data source is selected via the
    // [list:name] binding (see get_capabilities binding_schemes.list), not a
    // button JSON field. A note (not an "add"/name entry) keeps the schema-parity
    // lint honest — nothing here must map to a btn["…"] parse key.
    out["note"] = "data via [list:name] binding; no button-level config";
}
#endif
REGISTER_WIDGET_SCHEMA(list, nullptr, true);

#endif // HAS_DISPLAY
