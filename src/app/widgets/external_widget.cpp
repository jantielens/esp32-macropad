#include "widget.h"

#if HAS_NATIVE_EXTENSIONS

#include "external_widget.h"
#include "../native_extension.h"
#include "../screens/pad_screen.h"
#include <stddef.h>
#include <string.h>

struct ExternalWidgetState {
    lv_obj_t* root;
    uint32_t instance_id;
    uint32_t last_tick_ms;
    const ExternalWidgetConfig* config;
};

static_assert(sizeof(ExternalWidgetState) <= WIDGET_STATE_MAX_BYTES,
              "ExternalWidgetState exceeds WIDGET_STATE_MAX_BYTES");

static void external_parse(const JsonObject& btn, uint8_t* data) {
    auto* config = reinterpret_cast<ExternalWidgetConfig*>(data);
    strlcpy(config->extension_id, btn["extension_id"] | "", sizeof(config->extension_id));
    strlcpy(config->config, btn["extension_config"] | "", sizeof(config->config));
}

static void external_create(lv_obj_t* tile, const WidgetConfig* cfg,
                            const ScreenButtonConfig* btn, const PadRect* rect,
                            const UIScaleInfo* scale, lv_obj_t* icon_img,
                            lv_obj_t* center_label, WidgetState* state) {
    (void)cfg;
    (void)btn;
    (void)rect;
    (void)scale;
    auto* external = reinterpret_cast<ExternalWidgetState*>(state->data);
    memset(external, 0, sizeof(ExternalWidgetState));

    if (icon_img) lv_obj_add_flag(icon_img, LV_OBJ_FLAG_HIDDEN);
    if (center_label) lv_obj_add_flag(center_label, LV_OBJ_FLAG_HIDDEN);

    external->root = lv_obj_create(tile);
    lv_obj_set_size(external->root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(external->root);
    lv_obj_set_style_bg_opa(external->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(external->root, 0, 0);
    lv_obj_set_style_pad_all(external->root, 0, 0);
    lv_obj_remove_flag(external->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(external->root, LV_OBJ_FLAG_CLICKABLE);

    const ButtonTile* button = reinterpret_cast<const ButtonTile*>(
        reinterpret_cast<const uint8_t*>(state) - offsetof(ButtonTile, widget_state));
    const ExternalWidgetConfig* config = external_widget_config(cfg);
    external->config = config;
    external->instance_id = external_widget_instance_id(button->page, btn->col, btn->row);
    if (!native_extension_create_instance(config->extension_id, external->instance_id,
                                          external->root, config->config)) {
        lv_obj_t* label = lv_label_create(external->root);
        lv_label_set_text(label, "Extension unavailable");
        lv_obj_center(label);
    }
}

static void external_update(lv_obj_t* tile, const WidgetConfig* cfg,
                            WidgetState* state, const char* raw_value) {
    (void)tile;
    (void)cfg;
    (void)state;
    (void)raw_value;
}

static void external_destroy(WidgetState* state) {
    auto* external = reinterpret_cast<ExternalWidgetState*>(state->data);
    if (external->config) {
        native_extension_destroy_instance(external->config->extension_id, external->instance_id);
    }
    external->root = nullptr;
}

static void external_tick(lv_obj_t* tile, const WidgetConfig* cfg, WidgetState* state) {
    (void)tile;
    auto* external = reinterpret_cast<ExternalWidgetState*>(state->data);
    const uint32_t now = millis();
    if (now - external->last_tick_ms < 250) return;
    external->last_tick_ms = now;
    native_extension_tick_instance(external_widget_config(cfg)->extension_id, external->instance_id);
}

REGISTER_WIDGET(external, nullptr, false);

#endif