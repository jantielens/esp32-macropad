#pragma once

#include "../mouse_hid.h"
#include "../usb_hid.h"
#include <lvgl.h>

enum class MouseTouchEvent : uint8_t { Press, Position, Move, Release, Sample, End, Cancel };

struct MouseSurfaceTouch {
    static constexpr uint8_t contact_limit = 2;
    using PointHandler = void (*)(void*, MouseTouchEvent, uint8_t, const lv_point_t&);
    uint32_t epoch = 0;
    uint32_t generation = 0;
    uint32_t lifetime = 0;
    lv_obj_t* object = nullptr;
    PointHandler handler = nullptr;
    void* context = nullptr;
    bool enabled = true;
    bool physical = false;
    bool allow_replacement = false;

    static MouseSurfaceTouch*& session() {
        static MouseSurfaceTouch* active = nullptr;
        return active;
    }

    static uint32_t next_lifetime() {
        static uint32_t sequence = 0;
        if (!++sequence) ++sequence;
        return sequence;
    }

    static void metadata(lv_event_t* event) {
        auto* touch = static_cast<MouseSurfaceTouch*>(lv_event_get_user_data(event));
        touch->hide();
        touch->object = nullptr;
    }

    static MouseSurfaceTouch* find(lv_obj_t* target) {
        if (!target || !lv_obj_is_valid(target)) return nullptr;
        const uint32_t count = lv_obj_get_event_count(target);
        for (uint32_t index = 0; index < count; ++index) {
            lv_event_dsc_t* descriptor = lv_obj_get_event_dsc(target, index);
            if (lv_event_dsc_get_cb(descriptor) == metadata)
                return static_cast<MouseSurfaceTouch*>(lv_event_dsc_get_user_data(descriptor));
        }
        return nullptr;
    }

    bool dispatch(MouseTouchEvent interaction, uint8_t id, const lv_point_t& point, bool raw = true) {
        if (interaction == MouseTouchEvent::Cancel) { cancel(); return false; }
        if (!enabled || !mouse_hid_is_ready() ||
            (session() == this && generation != mouse_hid_generation())) {
            cancel();
            return false;
        }
        if (interaction == MouseTouchEvent::Press) {
            if (session() && (session() != this || physical != raw)) return false;
            if (!session()) {
                if (generation != mouse_hid_generation())
                    handler(context, MouseTouchEvent::Cancel, 0, lv_point_t{});
                session() = this;
                physical = raw;
                mouse_hid_begin_touch();
                epoch = usb_hid_epoch();
                generation = mouse_hid_generation();
            }
        } else if (session() != this || physical != raw) return false;
        handler(context, interaction, id, point);
        if (interaction == MouseTouchEvent::End) session() = nullptr;
        return true;
    }

    static bool is_pointer_event(lv_event_code_t code) {
        return code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING ||
               code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST ||
               code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_CLICKED ||
               code == LV_EVENT_LONG_PRESSED || code == LV_EVENT_LONG_PRESSED_REPEAT ||
               code == LV_EVENT_GESTURE;
    }

    void process(lv_event_t* event) {
        const lv_event_code_t code = lv_event_get_code(event);
        if (code == LV_EVENT_INDEV_RESET) {
            if (!physical) cancel();
            return;
        }
        if (!is_pointer_event(code)) return;
        lv_event_stop_bubbling(event);
        lv_event_stop_processing(event);
        if (session() && session()->physical) return;
        if (code == LV_EVENT_PRESS_LOST) { cancel(); return; }
        if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;
        lv_indev_t* indev = lv_indev_active();
        if (!indev) { cancel(); return; }
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        lv_obj_transform_point(object, &point, LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE_RECURSIVE);
        if (code == LV_EVENT_RELEASED) dispatch(MouseTouchEvent::Move, 0, point, false);
        dispatch(code == LV_EVENT_PRESSED ? MouseTouchEvent::Press :
                 code == LV_EVENT_RELEASED ? MouseTouchEvent::Release : MouseTouchEvent::Move,
                 0, point, false);
        if (code == LV_EVENT_RELEASED) dispatch(MouseTouchEvent::End, 0, point, false);
    }

    void cancel() {
        lifetime = next_lifetime();
        handler(context, MouseTouchEvent::Cancel, 0, lv_point_t{});
        if (session() == this || (!session() && epoch == usb_hid_epoch() &&
                                  generation == mouse_hid_generation())) {
            mouse_hid_cancel();
        }
        if (session() == this) session() = nullptr;
        physical = false;
    }

    void show() { cancel(); enabled = true; }
    void hide() { cancel(); enabled = false; }

    void detach(lv_event_cb_t callback) {
        hide();
        if (object) {
            lv_obj_remove_event_cb(object, callback);
            lv_obj_remove_event_cb(object, metadata);
        }
        object = nullptr;
    }

    void attach(lv_obj_t* button, lv_event_cb_t callback, void* state, PointHandler point_handler) {
        object = button;
        context = state;
        handler = point_handler;
        lifetime = next_lifetime();
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(button, callback, LV_EVENT_ALL, state);
        lv_obj_add_event_cb(button, metadata, LV_EVENT_DELETE, this);
    }
};