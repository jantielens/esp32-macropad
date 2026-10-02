#pragma once

#include "../mouse_hid.h"
#include "../ota_activity.h"
#include "../usb_hid.h"
#include <lvgl.h>

struct MouseSurfaceTouch {
    uint32_t epoch = 0;
    uint32_t ota_epoch = 0;

    template<typename Input>
    bool process(lv_event_t* event, Input& input, lv_point_t& point) {
        const lv_event_code_t code = lv_event_get_code(event);
        if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
            code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST &&
            code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_CLICKED &&
            code != LV_EVENT_LONG_PRESSED && code != LV_EVENT_LONG_PRESSED_REPEAT &&
            code != LV_EVENT_GESTURE) return false;
        lv_event_stop_bubbling(event);
        lv_event_stop_processing(event);
        if (code == LV_EVENT_PRESS_LOST || !mouse_hid_is_ready() ||
            (code != LV_EVENT_PRESSED &&
             (epoch != usb_hid_epoch() || ota_epoch != ota_activity_epoch()))) {
            hide(input);
            return false;
        }
        if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return false;
        lv_indev_t* indev = lv_indev_active();
        if (!indev) return false;
        lv_indev_get_point(indev, &point);
        if (code == LV_EVENT_PRESSED) {
            mouse_hid_begin_touch();
            epoch = usb_hid_epoch();
            ota_epoch = ota_activity_epoch();
            input.press(point.x, point.y, lv_tick_get());
            return false;
        }
        return true;
    }

    static void attach(lv_obj_t* button, lv_event_cb_t callback, void* state) {
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(button, callback, LV_EVENT_ALL, state);
    }

    template<typename Input>
    static void show(Input& input) {
        input.cancel();
    }

    template<typename Input>
    static void hide(Input& input) {
        show(input);
        mouse_hid_cancel();
    }
};