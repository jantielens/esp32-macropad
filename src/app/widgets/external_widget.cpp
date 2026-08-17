#include "widget.h"

#if HAS_NATIVE_EXTENSIONS

#include "external_widget.h"
#include "../native_extension.h"
#include "../screens/pad_screen.h"
#include <stddef.h>
#include <string.h>

struct ExternalWidgetState {
    lv_obj_t* root;
    lv_timer_t* timer;
    lv_obj_t* status_label;
    uint32_t instance_id;
    uint16_t tick_interval_ms;
    const ExternalWidgetConfig* config;
    bool created;
    bool retry_after_stop;
};

static_assert(sizeof(ExternalWidgetState) <= WIDGET_STATE_MAX_BYTES,
              "ExternalWidgetState exceeds WIDGET_STATE_MAX_BYTES");

static void external_parse(const JsonObject& btn, uint8_t* data) {
    auto* config = reinterpret_cast<ExternalWidgetConfig*>(data);
    strlcpy(config->extension_id, btn["extension_id"] | "", sizeof(config->extension_id));
    strlcpy(config->config, btn["extension_config"] | "", sizeof(config->config));
    const int32_t interval_ms = btn["extension_tick_interval_ms"] | 0;
    config->tick_interval_ms = interval_ms <= 0 ? 0 : static_cast<uint16_t>(clamp_val(
        interval_ms, static_cast<int32_t>(NATIVE_EXTENSION_TICK_INTERVAL_MIN_MS),
        static_cast<int32_t>(NATIVE_EXTENSION_TICK_INTERVAL_MAX_MS)));
}

static bool external_create_instance(ExternalWidgetState* external) {
    if (!external || !external->config || !external->root) return false;
    if (!native_extension_create_instance(external->config->extension_id, external->instance_id,
                                          external->root, external->config->config)) {
        external->retry_after_stop = native_extension_is_stopping(external->config->extension_id);
        if (external->status_label) {
            lv_label_set_text(external->status_label,
                              external->retry_after_stop ? "Extension restarting" : "Extension unavailable");
            lv_obj_center(external->status_label);
        }
        return false;
    }
    external->created = true;
    external->retry_after_stop = false;
    if (external->status_label) {
        lv_obj_delete(external->status_label);
        external->status_label = nullptr;
    }
    return true;
}

static void external_timer_cb(lv_timer_t* timer) {
    auto* external = static_cast<ExternalWidgetState*>(lv_timer_get_user_data(timer));
    if (!external || !external->config) return;
    if (!external->created) {
        if (external->retry_after_stop) external_create_instance(external);
        return;
    }
    native_extension_tick_instance(external->config->extension_id, external->instance_id);
}

static void external_create(lv_obj_t* tile, const WidgetConfig* cfg,
                            const ScreenButtonConfig* btn, const PadRect* rect,
                            const UIScaleInfo* scale, lv_obj_t* icon_img,
                            lv_obj_t* center_label, WidgetState* state) {
    (void)cfg;
    (void)btn;
    (void)scale;
    auto* external = reinterpret_cast<ExternalWidgetState*>(state->data);
    memset(external, 0, sizeof(ExternalWidgetState));

    if (icon_img) lv_obj_add_flag(icon_img, LV_OBJ_FLAG_HIDDEN);
    if (center_label) lv_obj_add_flag(center_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_update_layout(tile);
    external->root = lv_obj_create(tile);
    lv_obj_set_size(external->root, lv_obj_get_content_width(tile), lv_obj_get_content_height(tile));
    lv_obj_set_pos(external->root, 0, 0);
    lv_obj_set_style_bg_opa(external->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(external->root, 0, 0);
    lv_obj_set_style_pad_all(external->root, 0, 0);
    lv_obj_remove_flag(external->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(external->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_update_layout(external->root);

    const ButtonTile* button = reinterpret_cast<const ButtonTile*>(
        reinterpret_cast<const uint8_t*>(state) - offsetof(ButtonTile, widget_state));
    const ExternalWidgetConfig* config = external_widget_config(cfg);
    external->config = config;
    external->tick_interval_ms = config->tick_interval_ms
        ? config->tick_interval_ms
        : native_extension_tick_interval_ms(config->extension_id);
    external->instance_id = external_widget_instance_id(button->page, btn->col, btn->row);
    native_extension_set_instance_binding_context(config->extension_id, external->instance_id,
                                                  button->pad_bindings, button->pad_binding_count);
    native_extension_set_instance_button_context(config->extension_id, external->instance_id, tile,
                                                 button->label_top, button->label_center, button->label_bottom);
        LOGI("Extensions", "Create %s instance=%08lx root=%dx%d rect=%ux%u",
            config->extension_id, static_cast<unsigned long>(external->instance_id),
            lv_obj_get_width(external->root), lv_obj_get_height(external->root), rect->w, rect->h);
    external->status_label = lv_label_create(external->root);
    external_create_instance(external);
    external->timer = lv_timer_create(external_timer_cb, external->tick_interval_ms, external);
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
    if (external->timer) {
        lv_timer_delete(external->timer);
        external->timer = nullptr;
    }
    if (external->config && external->created) {
        native_extension_destroy_instance(external->config->extension_id, external->instance_id);
    }
    if (external->config) {
        native_extension_clear_instance_binding_context(external->config->extension_id, external->instance_id);
    }
    external->root = nullptr;
}

static void external_tick(lv_obj_t* tile, const WidgetConfig* cfg, WidgetState* state) {
    (void)tile;
    (void)cfg;
    (void)state;
}

REGISTER_WIDGET(external, nullptr, false);

#endif