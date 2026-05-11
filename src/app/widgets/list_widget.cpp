#include "widget.h"

#if HAS_DISPLAY

#include "../list_provider.h"
#include "../action_parse.h"
#include "../action_dispatch.h"
#include <string.h>

// ============================================================================
// List Widget — scrollable tappable list from a ListProvider
// ============================================================================

struct ListWidgetConfig {
    ButtonAction select_action;
};

static_assert(sizeof(ListWidgetConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "ListWidgetConfig exceeds WIDGET_CONFIG_MAX_BYTES");

struct ListWidgetState {
    const ListWidgetConfig* cfg;
    char (*ids)[LIST_ITEM_ID_MAX];   // heap-allocated array of item ID strings
    uint8_t count;
};

static_assert(sizeof(ListWidgetState) <= WIDGET_STATE_MAX_BYTES,
              "ListWidgetState exceeds WIDGET_STATE_MAX_BYTES");

// ---- {id} substitution (mirrors numericrocker {step} pattern) ----

static void list_substitute_id(ButtonAction* act, const char* id) {
    list_substitute_id_in_field(act->mqtt_payload,    sizeof(act->mqtt_payload),    id);
    list_substitute_id_in_field(act->key_sequence,     sizeof(act->key_sequence),     id);
    list_substitute_id_in_field(act->volume_value,     sizeof(act->volume_value),     id);
    list_substitute_id_in_field(act->brightness_value, sizeof(act->brightness_value), id);
    list_substitute_id_in_field(act->timer_value,      sizeof(act->timer_value),      id);
}

// ---- Click handler ----

static void list_item_clicked(lv_event_t* e) {
    auto* st = reinterpret_cast<ListWidgetState*>(lv_event_get_user_data(e));
    if (!st || !st->cfg) return;
    if (st->cfg->select_action.type[0] == '\0') return;

    lv_obj_t* target = lv_event_get_target(e);
    uintptr_t idx = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target));
    if (idx >= st->count) return;

    ButtonAction local;
    memcpy(&local, &st->cfg->select_action, sizeof(ButtonAction));
    list_substitute_id(&local, st->ids[idx]);
    action_dispatch(local, "ListSelect");
}

// ---- WidgetType callbacks ----

static void list_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<ListWidgetConfig*>(data);
    memset(cfg, 0, sizeof(ListWidgetConfig));

    JsonObject act = btn["widget_list_action"];
    if (!act.isNull()) {
        action_parse(act, cfg->select_action);
    }
}

static void list_create(lv_obj_t* tile, const WidgetConfig* wcfg,
                         const ScreenButtonConfig* btn,
                         const PadRect* rect, const UIScaleInfo* scale,
                         lv_obj_t* icon_img, lv_obj_t* center_label,
                         WidgetState* state) {
    (void)btn;
    auto* cfg = reinterpret_cast<const ListWidgetConfig*>(wcfg->data);
    auto* st = reinterpret_cast<ListWidgetState*>(state->data);
    memset(st, 0, sizeof(ListWidgetState));
    st->cfg = cfg;

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

    // Create scrollable LVGL list
    lv_obj_t* list = lv_list_create(tile);
    lv_obj_set_size(list, rect->w, rect->h);
    lv_obj_center(list);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_style_pad_gap(list, 2, 0);

    for (uint8_t i = 0; i < count; i++) {
        lv_obj_t* item = lv_list_add_btn(list, NULL, items[i].label);
        lv_obj_set_user_data(item, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(item, list_item_clicked, LV_EVENT_CLICKED, st);

        lv_obj_set_style_bg_opa(item, LV_OPA_50, 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x444444), 0);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x666666), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(item, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(item, scale->font_small, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 4, 0);
        lv_obj_set_style_pad_ver(item, 6, 0);
    }
}

static void list_update(lv_obj_t* tile, const WidgetConfig* cfg,
                         WidgetState* state, const char* raw_value) {
    (void)tile; (void)cfg; (void)state; (void)raw_value;
}

static void list_destroy(WidgetState* state) {
    auto* st = reinterpret_cast<ListWidgetState*>(state->data);
    if (st->ids) {
        free(st->ids);
        st->ids = nullptr;
    }
}

// ---- Registration ----

REGISTER_WIDGET(list, nullptr, true);

#endif // HAS_DISPLAY
