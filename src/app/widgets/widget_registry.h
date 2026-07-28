#ifndef WIDGETS_WIDGET_REGISTRY_H
#define WIDGETS_WIDGET_REGISTRY_H

#include "../board_config.h"

#if HAS_DISPLAY

#include "../pad_config.h"
#include <ArduinoJson.h>
#include <stdint.h>

typedef struct _lv_obj_t lv_obj_t;
struct PadRect;
struct UIScaleInfo;

#define WIDGET_STATE_MAX_BYTES 256

struct WidgetState {
    uint8_t data[WIDGET_STATE_MAX_BYTES];
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
                            uint32_t* window_secs, uint8_t* slot_count,
                            const char** out_binding,
                            const char** out_ha_entity, uint8_t* out_ha_stat);
    bool resolveInTick;
    void (*describeSchema)(JsonObject& out);
};

const WidgetType* widget_find(const char* type_name);
void widget_register(const WidgetType* type);
uint8_t widget_count();
const WidgetType* widget_at(uint8_t index);

#endif // HAS_DISPLAY

#endif // WIDGETS_WIDGET_REGISTRY_H