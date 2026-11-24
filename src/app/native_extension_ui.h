#pragma once

#include "native_extension_api.h"
#include <lvgl.h>

static inline bool native_extension_ui_align(void* object, NativeExtensionAlign align, int32_t x, int32_t y) {
    if (!object) return false;
    lv_align_t lv_align;
    switch (align) {
        case NATIVE_EXTENSION_ALIGN_TOP_LEFT: lv_align = LV_ALIGN_TOP_LEFT; break;
        case NATIVE_EXTENSION_ALIGN_TOP_MID: lv_align = LV_ALIGN_TOP_MID; break;
        case NATIVE_EXTENSION_ALIGN_TOP_RIGHT: lv_align = LV_ALIGN_TOP_RIGHT; break;
        case NATIVE_EXTENSION_ALIGN_LEFT_MID: lv_align = LV_ALIGN_LEFT_MID; break;
        case NATIVE_EXTENSION_ALIGN_CENTER: lv_align = LV_ALIGN_CENTER; break;
        case NATIVE_EXTENSION_ALIGN_RIGHT_MID: lv_align = LV_ALIGN_RIGHT_MID; break;
        case NATIVE_EXTENSION_ALIGN_BOTTOM_LEFT: lv_align = LV_ALIGN_BOTTOM_LEFT; break;
        case NATIVE_EXTENSION_ALIGN_BOTTOM_MID: lv_align = LV_ALIGN_BOTTOM_MID; break;
        case NATIVE_EXTENSION_ALIGN_BOTTOM_RIGHT: lv_align = LV_ALIGN_BOTTOM_RIGHT; break;
        default: return false;
    }
    lv_obj_align(static_cast<lv_obj_t*>(object), lv_align, x, y);
    return true;
}

static inline void* native_extension_ui_button_create(void* parent) {
    lv_obj_t* button = parent ? lv_button_create(static_cast<lv_obj_t*>(parent)) : nullptr;
    if (button) {
        lv_obj_remove_style_all(button);
        lv_obj_set_scrollable(button, false);
    }
    return button;
}

static inline void native_extension_ui_button_set_press_feedback(void* button, bool enabled) {
    if (!button) return;
    auto* object = static_cast<lv_obj_t*>(button);
    if (!enabled) {
        lv_obj_remove_style(object, nullptr, LV_PART_MAIN | LV_STATE_PRESSED);
        return;
    }
    const lv_color_t background = lv_obj_get_style_bg_color(object, LV_PART_MAIN);
    const lv_color_t foreground = lv_obj_get_style_text_color(object, LV_PART_MAIN);
    lv_obj_set_style_bg_color(object, foreground, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(object, background, LV_STATE_PRESSED);
}

static inline lv_event_code_t native_extension_lvgl_event_code(NativeExtensionLvglEventCode code) {
    switch (code) {
        case NATIVE_EXTENSION_EVENT_PRESSED: return LV_EVENT_PRESSED;
        case NATIVE_EXTENSION_EVENT_PRESSING: return LV_EVENT_PRESSING;
        case NATIVE_EXTENSION_EVENT_RELEASED: return LV_EVENT_RELEASED;
        case NATIVE_EXTENSION_EVENT_CLICKED: return LV_EVENT_CLICKED;
        case NATIVE_EXTENSION_EVENT_LONG_PRESSED: return LV_EVENT_LONG_PRESSED;
        case NATIVE_EXTENSION_EVENT_VALUE_CHANGED: return LV_EVENT_VALUE_CHANGED;
        case NATIVE_EXTENSION_EVENT_DELETE: return LV_EVENT_DELETE;
        case NATIVE_EXTENSION_EVENT_PRESS_LOST: return LV_EVENT_PRESS_LOST;
    }
    return LV_EVENT_ALL;
}

static inline NativeExtensionLvglEventCode native_extension_ui_event_code(lv_event_code_t code) {
    switch (code) {
        case LV_EVENT_PRESSED: return NATIVE_EXTENSION_EVENT_PRESSED;
        case LV_EVENT_PRESSING: return NATIVE_EXTENSION_EVENT_PRESSING;
        case LV_EVENT_RELEASED: return NATIVE_EXTENSION_EVENT_RELEASED;
        case LV_EVENT_CLICKED: return NATIVE_EXTENSION_EVENT_CLICKED;
        case LV_EVENT_LONG_PRESSED: return NATIVE_EXTENSION_EVENT_LONG_PRESSED;
        case LV_EVENT_VALUE_CHANGED: return NATIVE_EXTENSION_EVENT_VALUE_CHANGED;
        case LV_EVENT_PRESS_LOST: return NATIVE_EXTENSION_EVENT_PRESS_LOST;
        default: return NATIVE_EXTENSION_EVENT_DELETE;
    }
}

static inline void native_extension_ui_event(lv_event_t* event) {
    auto* binding = static_cast<NativeExtensionUiEventBinding*>(lv_event_get_user_data(event));
    if (binding && binding->callback)
        binding->callback(binding->user_data, lv_event_get_target(event),
                          native_extension_ui_event_code(lv_event_get_code(event)));
}

static inline void* native_extension_ui_event_add(void* object, NativeExtensionLvglEventCode code,
                                                  NativeExtensionUiEventBinding* binding) {
    const lv_event_code_t filter = native_extension_lvgl_event_code(code);
    if (!object || !binding || !binding->callback || filter == LV_EVENT_ALL) return nullptr;
    return lv_obj_add_event_cb(static_cast<lv_obj_t*>(object), native_extension_ui_event, filter, binding);
}

static inline bool native_extension_ui_event_remove(void* object, void* handler) {
    return object && handler &&
           lv_obj_remove_event_dsc(static_cast<lv_obj_t*>(object), static_cast<lv_event_dsc_t*>(handler));
}
