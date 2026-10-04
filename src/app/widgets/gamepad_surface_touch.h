#pragma once

#include "../gamepad_hid.h"
#include "../log_manager.h"
#include <lvgl.h>

enum class GamepadTouchEvent : uint8_t { Ignore, Press, Move, Release, Cancel };

struct GamepadSurfaceTouch {
    using PointHandler = void (*)(void*, GamepadTouchEvent, const lv_point_t&);
    uint32_t owner = 0;
    uint32_t generation = 0;
    uint32_t lifetime = 0;
    lv_obj_t* object = nullptr;
    PointHandler handler = nullptr;
    void* context = nullptr;
    bool enabled = true;

    static uint32_t next_lifetime() {
        static uint32_t sequence = 0;
        if (!++sequence) ++sequence;
        return sequence;
    }

    void availability(bool available) {
        enabled = available;
        lifetime = next_lifetime();
    }

    static void metadata(lv_event_t* event) {
        if (lv_event_get_code(event) != LV_EVENT_DELETE) return;
        auto* touch = static_cast<GamepadSurfaceTouch*>(lv_event_get_user_data(event));
        touch->object = nullptr;
        touch->availability(false);
        touch->handler(touch->context, GamepadTouchEvent::Cancel, lv_point_t{});
    }

    static GamepadSurfaceTouch* find(lv_obj_t* object) {
        if (!object || !lv_obj_is_valid(object)) return nullptr;
        const uint32_t event_count = lv_obj_get_event_count(object);
        for (uint32_t index = 0; index < event_count; ++index) {
            lv_event_dsc_t* descriptor = lv_obj_get_event_dsc(object, index);
            if (lv_event_dsc_get_cb(descriptor) == metadata)
                return static_cast<GamepadSurfaceTouch*>(lv_event_dsc_get_user_data(descriptor));
        }
        return nullptr;
    }

    void hide() {
        availability(false);
        handler(context, GamepadTouchEvent::Cancel, lv_point_t{});
    }

    void show() {
        hide();
        availability(true);
    }

    void detach(lv_event_cb_t callback) {
        hide();
        if (object) {
            lv_obj_remove_event_cb(object, callback);
            lv_obj_remove_event_cb(object, metadata);
        }
        object = nullptr;
        availability(false);
    }

    GamepadTouchEvent point_event(GamepadTouchEvent interaction) {
        if (interaction == GamepadTouchEvent::Ignore) return interaction;
        if (interaction == GamepadTouchEvent::Cancel) return interaction;
        if (!gamepad_hid_is_ready()) {
            if (interaction == GamepadTouchEvent::Press) LOGW("GamepadTouch", "Press rejected: USB gamepad not ready");
            return GamepadTouchEvent::Cancel;
        }
        if (interaction == GamepadTouchEvent::Press) generation = gamepad_hid_generation();
        else if (generation != gamepad_hid_generation()) return GamepadTouchEvent::Cancel;
        if (interaction != GamepadTouchEvent::Press && !owner) return GamepadTouchEvent::Ignore;
        return interaction;
    }

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
        if (code == LV_EVENT_PRESS_LOST) return GamepadTouchEvent::Cancel;
        if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED)
            return GamepadTouchEvent::Ignore;
        lv_indev_t* indev = lv_indev_active();
        if (!indev) {
            if (code == LV_EVENT_PRESSED) LOGW("GamepadTouch", "Press rejected: no active touch input");
            return GamepadTouchEvent::Cancel;
        }
        lv_indev_get_point(indev, &point);
        return code == LV_EVENT_PRESSED ? GamepadTouchEvent::Press :
            code == LV_EVENT_RELEASED ? GamepadTouchEvent::Release : GamepadTouchEvent::Move;
    }

    void cancel() {
        if (owner) gamepad_hid_release(owner, generation);
        owner = 0;
    }

    void attach(lv_obj_t* button, lv_event_cb_t callback, void* state, PointHandler point_handler) {
        object = button;
        context = state;
        handler = point_handler;
        availability(true);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(button, callback, LV_EVENT_ALL, state);
        lv_obj_add_event_cb(button, metadata, LV_EVENT_DELETE, this);
    }
};