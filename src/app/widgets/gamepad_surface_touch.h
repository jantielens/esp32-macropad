#pragma once

#include "../gamepad_hid.h"
#include "../log_manager.h"
#include <lvgl.h>

enum class GamepadTouchEvent : uint8_t { Ignore, Press, Move, Release, Cancel };

struct GamepadSurfaceTouch {
    uint32_t owner = 0;
    uint32_t generation = 0;

    GamepadTouchEvent process(lv_event_t* event, lv_point_t& point) {
        const lv_event_code_t code = lv_event_get_code(event);
        if (code == LV_EVENT_INDEV_RESET) return GamepadTouchEvent::Cancel;
        if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
            code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST &&
            code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_CLICKED &&
            code != LV_EVENT_LONG_PRESSED && code != LV_EVENT_LONG_PRESSED_REPEAT &&
            code != LV_EVENT_GESTURE) return GamepadTouchEvent::Ignore;
        lv_event_stop_bubbling(event);
        lv_event_stop_processing(event);
        if (code == LV_EVENT_PRESS_LOST || !gamepad_hid_is_ready()) {
            if (code == LV_EVENT_PRESSED) LOGW("GamepadTouch", "Press rejected: USB gamepad not ready");
            return GamepadTouchEvent::Cancel;
        }
        if (code == LV_EVENT_PRESSED) generation = gamepad_hid_generation();
        else if (generation != gamepad_hid_generation()) return GamepadTouchEvent::Cancel;
        if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED)
            return GamepadTouchEvent::Ignore;
        lv_indev_t* indev = lv_indev_active();
        if (!indev) {
            if (code == LV_EVENT_PRESSED) LOGW("GamepadTouch", "Press rejected: no active touch input");
            return GamepadTouchEvent::Cancel;
        }
        lv_indev_get_point(indev, &point);
        if (code == LV_EVENT_PRESSED) return GamepadTouchEvent::Press;
        if (!owner) return GamepadTouchEvent::Ignore;
        return code == LV_EVENT_RELEASED ? GamepadTouchEvent::Release : GamepadTouchEvent::Move;
    }

    void cancel() {
        if (owner) gamepad_hid_release(owner, generation);
        owner = 0;
    }

    static void attach(lv_obj_t* button, lv_event_cb_t callback, void* state) {
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(button, callback, LV_EVENT_ALL, state);
    }
};