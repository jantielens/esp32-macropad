#pragma once

#include "gamepad_surface_touch.h"
#include "mouse_surface_touch.h"
#include "../touch_sample.h"

class GamepadTouchRouter {
public:
    bool reset_navigation = false;

    void cancel(bool require_release = true) {
        blocked_ = blocked_ || require_release;
        end_mouse(MouseTouchEvent::Cancel);
        for (auto& slot : slots_) {
            blocked_ = blocked_ || slot.active;
            dispatch(slot, GamepadTouchEvent::Cancel);
            slot = Slot{};
        }
        controller_session_ = false;
        reset_navigation = true;
    }

    TouchSample update(const TouchSnapshot& snapshot, bool suppressed, uint32_t generation,
                       uint32_t mouse_generation = 0) {
        reset_navigation = false;
        TouchSample navigation;
        if (generation_initialized_ &&
            (generation != generation_ || mouse_generation != mouse_generation_))
            cancel(snapshot.status == TouchReadStatus::Error);
        generation_initialized_ = true;
        generation_ = generation;
        mouse_generation_ = mouse_generation;
        if (suppressed) cancel();
        if (blocked_) {
            if (!suppressed && snapshot.status == TouchReadStatus::Fresh && !snapshot.count)
                blocked_ = false;
            return navigation;
        }
        if (snapshot.status != TouchReadStatus::Fresh) return navigation_state();

        uint16_t ids = 0;
        if (snapshot.count > TOUCH_CONTACT_CAPACITY) { cancel(); return navigation; }
        for (uint8_t index = 0; index < snapshot.count; ++index) {
            const uint8_t id = snapshot.contacts[index].id;
            const uint16_t mask = touch_contact_id_mask(id);
            if (!mask || (ids & mask)) { cancel(); return navigation; }
            ids |= mask;
        }
        bool navigation_released = false;
        if (mouse_object_) {
            auto* mouse = resolve_mouse();
            if (!mouse || !mouse->enabled || !lv_obj_is_visible(mouse_object_) ||
                lv_obj_has_state(mouse_object_, LV_STATE_DISABLED)) { cancel(); return navigation; }
        }
        for (auto& slot : slots_) {
            if (!slot.active) continue;
            GamepadSurfaceTouch* touch = slot.mouse ? nullptr : resolve_target(slot);
            if (slot.object && !slot.mouse && (!touch || !touch->enabled || !lv_obj_is_visible(slot.object) ||
                lv_obj_has_state(slot.object, LV_STATE_DISABLED))) { cancel(); return navigation; }
            if (!(ids & touch_contact_id_mask(slot.id))) {
                navigation_released |= slot.navigation;
                if (slot.mouse) {
                    dispatch(slot, GamepadTouchEvent::Release);
                    auto* mouse = resolve_mouse();
                    if (!mouse || !mouse->allow_replacement) mouse_stopped_ = true;
                } else dispatch(slot, GamepadTouchEvent::Release, touch);
                slot = Slot{};
            } else if (touch || slot.mouse) {
                for (uint8_t index = 0; index < snapshot.count; ++index) {
                    const TouchContact& contact = snapshot.contacts[index];
                    if (contact.id != slot.id) continue;
                    if (slot.point.x != contact.horizontal || slot.point.y != contact.vertical) {
                        slot.point = {contact.horizontal, contact.vertical};
                        if (slot.mouse) {
                            if (auto* mouse = resolve_mouse()) {
                                lv_point_t point = slot.point;
                                lv_obj_transform_point(slot.object, &point, LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE_RECURSIVE);
                                mouse->dispatch(MouseTouchEvent::Position, slot.id, point);
                            }
                            slot.changed = true;
                        } else dispatch(slot, GamepadTouchEvent::Move, touch);
                    }
                    break;
                }
            }
        }
        if (!snapshot.count) {
            end_mouse(MouseTouchEvent::End);
            controller_session_ = false;
            return navigation;
        }

        for (auto& slot : slots_) slot.new_contact = false;
        for (uint8_t id = 0; id < TOUCH_TRACKING_ID_COUNT; ++id) {
            const TouchContact* contact = nullptr;
            for (uint8_t index = 0; index < snapshot.count; ++index)
                if (snapshot.contacts[index].id == id) contact = &snapshot.contacts[index];
            if (!contact) continue;
            Slot* slot = nullptr;
            for (auto& existing : slots_) if (existing.active && existing.id == id) slot = &existing;
            if (slot) {
                slot->point = {contact->horizontal, contact->vertical};
                continue;
            }
            for (auto& empty : slots_) if (!empty.active) { slot = &empty; break; }
            if (!slot) { cancel(); return navigation; }
            slot->active = true;
            slot->new_contact = true;
            slot->id = id;
            slot->point = {contact->horizontal, contact->vertical};
            lv_obj_t* target = hit_target(slot->point);
            MouseSurfaceTouch* mouse = MouseSurfaceTouch::find(target);
            if (mouse) {
                controller_session_ = true;
                reset_navigation = true;
                if (!mouse->enabled || lv_obj_has_state(target, LV_STATE_DISABLED) || mouse_stopped_ ||
                    (mouse_object_ && mouse_object_ != target)) continue;
                unsigned captured = 0;
                for (const auto& existing : slots_) captured += existing.mouse;
                if (captured >= MouseSurfaceTouch::contact_limit) continue;
                if (!mouse_object_) {
                    mouse_object_ = target;
                    mouse_lifetime_ = mouse->lifetime;
                }
                slot->object = target;
                slot->mouse = true;
                if (!dispatch_mouse(*slot, GamepadTouchEvent::Press, mouse) || !resolve_mouse()) {
                    cancel();
                    return navigation;
                }
                continue;
            }
            GamepadSurfaceTouch* touch = GamepadSurfaceTouch::find(target);
            if (!touch) continue;
            controller_session_ = true;
            reset_navigation = true;
            bool occupied = false;
            for (const auto& existing : slots_) if (existing.object == target) occupied = true;
            if (occupied || !touch->enabled || lv_obj_has_state(target, LV_STATE_DISABLED)) continue;
            slot->object = target;
            slot->lifetime = touch->lifetime;
            dispatch(*slot, GamepadTouchEvent::Press, touch);
            touch = GamepadSurfaceTouch::find(target);
            if (!touch || touch->lifetime != slot->lifetime) { cancel(); return navigation; }
            if (!touch->owner) slot->object = nullptr;
        }

        for (auto& slot : slots_) {
            if (!slot.new_contact && slot.changed) dispatch(slot, GamepadTouchEvent::Move);
            slot.changed = false;
        }
        if (mouse_object_) {
            auto* mouse = resolve_mouse();
            if (!mouse || !mouse->dispatch(MouseTouchEvent::Sample, 0, lv_point_t{})) {
                cancel();
                return navigation;
            }
        }
        if (controller_session_) {
            for (auto& slot : slots_) slot.navigation = false;
            return navigation;
        }
        bool has_navigation = false;
        for (const auto& slot : slots_) has_navigation |= slot.navigation;
        if (!has_navigation) {
            for (auto& slot : slots_) {
                if (slot.active && slot.new_contact) { slot.navigation = true; break; }
            }
        }
        return navigation_released ? TouchSample{} : navigation_state();
    }

private:
    struct Slot {
        lv_obj_t* object = nullptr;
        uint32_t lifetime = 0;
        lv_point_t point{};
        uint8_t id = 0;
        bool active = false;
        bool navigation = false;
        bool new_contact = false;
        bool changed = false;
        bool mouse = false;
    };
    Slot slots_[TOUCH_CONTACT_CAPACITY]{};
    uint32_t generation_ = 0;
    uint32_t mouse_generation_ = 0;
    lv_obj_t* mouse_object_ = nullptr;
    uint32_t mouse_lifetime_ = 0;
    bool mouse_stopped_ = false;
    bool generation_initialized_ = false;
    bool blocked_ = false;
    bool controller_session_ = false;

    MouseSurfaceTouch* resolve_mouse() const {
        auto* mouse = MouseSurfaceTouch::session();
        return mouse && mouse->object == mouse_object_ && mouse->lifetime == mouse_lifetime_ ? mouse : nullptr;
    }

    void end_mouse(MouseTouchEvent interaction) {
        if (auto* mouse = resolve_mouse()) mouse->dispatch(interaction, 0, lv_point_t{});
        mouse_object_ = nullptr;
        mouse_stopped_ = false;
    }

    static lv_obj_t* hit_target(lv_point_t point) {
        lv_display_t* display = lv_display_get_default();
        if (!display) return nullptr;
        lv_obj_t* target = lv_indev_search_obj(lv_display_get_layer_sys(display), &point);
        if (!target) target = lv_indev_search_obj(lv_display_get_layer_top(display), &point);
        if (!target) target = lv_indev_search_obj(lv_display_get_screen_active(display), &point);
        return target;
    }

    static GamepadSurfaceTouch* resolve_target(const Slot& slot) {
        GamepadSurfaceTouch* touch = GamepadSurfaceTouch::find(slot.object);
        return touch && touch->lifetime == slot.lifetime ? touch : nullptr;
    }

    void dispatch(const Slot& slot, GamepadTouchEvent interaction) {
        if (slot.mouse) {
            dispatch_mouse(slot, interaction, resolve_mouse());
            return;
        }
        dispatch(slot, interaction, resolve_target(slot));
    }

    static bool dispatch_mouse(const Slot& slot, GamepadTouchEvent interaction, MouseSurfaceTouch* mouse) {
        if (!mouse) return false;
        lv_point_t point = slot.point;
        lv_obj_transform_point(slot.object, &point, LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE_RECURSIVE);
        return mouse->dispatch(interaction == GamepadTouchEvent::Press ? MouseTouchEvent::Press :
                               interaction == GamepadTouchEvent::Move ? MouseTouchEvent::Move :
                               interaction == GamepadTouchEvent::Release ? MouseTouchEvent::Release :
                               MouseTouchEvent::Cancel, slot.id, point);
    }

    static void dispatch(const Slot& slot, GamepadTouchEvent interaction, GamepadSurfaceTouch* touch) {
        if (!touch) return;
        lv_point_t point = slot.point;
        lv_obj_transform_point(slot.object, &point, LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE_RECURSIVE);
        touch->handler(touch->context, interaction, point);
    }

    TouchSample navigation_state() const {
        TouchSample result;
        for (const auto& slot : slots_) {
            if (!slot.navigation) continue;
            result.pressed = true;
            result.horizontal = slot.point.x;
            result.vertical = slot.point.y;
        }
        return result;
    }
};