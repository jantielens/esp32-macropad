#ifndef WIDGETS_WIDGET_REGISTRY_H
#define WIDGETS_WIDGET_REGISTRY_H

#include "../board_config.h"

#if HAS_DISPLAY

#include "../pad_config.h"
#include <ArduinoJson.h>
#include <stdint.h>
#include <string.h>

typedef struct _lv_obj_t lv_obj_t;
struct PadRect;
struct UIScaleInfo;

#define WIDGET_STATE_MAX_BYTES 256

struct WidgetState {
    uint8_t data[WIDGET_STATE_MAX_BYTES];
};

struct WidgetPreview {
    const char* name;
    const char* icon;
    const char* second_icon;
    const char* axis_field;
    const char* horizontal_icon;
    const char* vertical_icon;
    const char* default_axis;

    constexpr WidgetPreview(const char* preview_name = nullptr,
                            const char* preview_icon = nullptr,
                            const char* preview_second_icon = nullptr,
                            const char* preview_axis_field = nullptr,
                            const char* preview_horizontal_icon = nullptr,
                            const char* preview_vertical_icon = nullptr,
                            const char* preview_default_axis = nullptr)
        : name(preview_name), icon(preview_icon), second_icon(preview_second_icon),
         axis_field(preview_axis_field), horizontal_icon(preview_horizontal_icon),
         vertical_icon(preview_vertical_icon), default_axis(preview_default_axis) {}
};

struct WidgetType {
    const char* name;
    void (*parseConfig)(const JsonObject& btn, uint8_t* data);
    void (*createUI)(lv_obj_t* tile, const WidgetConfig* cfg,
                     const struct ScreenButtonConfig* btn,
                     const PadRect* rect, const UIScaleInfo* scale,
                     lv_obj_t* icon_img, lv_obj_t* center_label,
                     WidgetState* state);
    void (*update)(lv_obj_t* tile, const WidgetConfig* cfg,
                   WidgetState* state, const char* raw_value);
    void (*destroyUI)(WidgetState* state);
    void (*tick)(lv_obj_t* tile, const WidgetConfig* cfg, WidgetState* state);
    // Describe stream `stream_index` (false = widget has no further streams).
    // `out_ha_entity` / `out_ha_stat` name the optional Home Assistant history
    // source; widgets without one report "" / 0.
    bool (*getStreamParams)(const WidgetConfig* cfg, uint8_t stream_index,
                            uint32_t* window_secs, uint16_t* slot_count,
                            const char** out_binding,
                            const char** out_ha_entity, uint8_t* out_ha_stat);
    bool resolveInTick;
    void (*describeSchema)(JsonObject& out);
    void (*onShow)(WidgetState* state);
    void (*onHide)(WidgetState* state);
    const char* (*validateConfig)(JsonObjectConst button);
    const WidgetPreview* preview;
};

const WidgetType* widget_find(const char* type_name);
void widget_register(const WidgetType* type);
uint8_t widget_count();
const WidgetType* widget_at(uint8_t index);
void widget_preview_catalog_emit(JsonArray catalog);

inline bool widget_supports_pad_swipe_control(const char* type) {
    return strcmp(type, "mousepad") == 0 || strcmp(type, "scrollpad") == 0 ||
           strcmp(type, "gamepad_button") == 0 || strcmp(type, "gamepad_stick") == 0;
}

inline bool widget_disables_pad_swipes(const WidgetConfig& config) {
    return config.disable_pad_swipes && widget_supports_pad_swipe_control(config.type);
}

inline bool pad_disables_swipes(const PadConfig& config) {
    for (uint8_t index = 0; index < config.button_count; ++index) {
        if (widget_disables_pad_swipes(config.buttons[index].widget)) return true;
    }
    return false;
}

#endif // HAS_DISPLAY

#endif // WIDGETS_WIDGET_REGISTRY_H