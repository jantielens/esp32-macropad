#pragma once

#include "gamepad_surface_touch.h"
#include "../touch_sample.h"

class GamepadTouchRouter {
public:
    bool reset_navigation = false;

    void cancel() {
        for (auto& slot : slots_) {
            dispatch(slot, GamepadTouchEvent::Cancel);
            slot = Slot{};
        }
        blocked_ = true;
        controller_session_ = false;
        reset_navigation = true;
    }

    TouchSample update(const TouchSnapshot& snapshot, bool suppressed, uint32_t generation) {
        reset_navigation = false;
        TouchSample navigation;
        if (generation_initialized_ && generation != generation_) cancel();
        generation_initialized_ = true;
        generation_ = generation;
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
        for (auto& slot : slots_) {
            if (!slot.active) continue;
            GamepadSurfaceTouch* touch = resolve_target(slot);
            if (slot.object && (!touch || !touch->enabled || !lv_obj_is_visible(slot.object) ||
                lv_obj_has_state(slot.object, LV_STATE_DISABLED))) { cancel(); return navigation; }
            if (!(ids & touch_contact_id_mask(slot.id))) {
                navigation_released |= slot.navigation;
                dispatch(slot, GamepadTouchEvent::Release, touch);
                slot = Slot{};
            } else if (touch) {
                for (uint8_t index = 0; index < snapshot.count; ++index) {
                    const TouchContact& contact = snapshot.contacts[index];
                    if (contact.id != slot.id) continue;
                    if (slot.point.x != contact.horizontal || slot.point.y != contact.vertical) {
                        slot.point = {contact.horizontal, contact.vertical};
                        dispatch(slot, GamepadTouchEvent::Move, touch);
                    }
                    break;
                }
            }
        }
        if (!snapshot.count) {
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
    };
    Slot slots_[TOUCH_CONTACT_CAPACITY]{};
    uint32_t generation_ = 0;
    bool generation_initialized_ = false;
    bool blocked_ = false;
    bool controller_session_ = false;

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

    static void dispatch(const Slot& slot, GamepadTouchEvent interaction) {
        dispatch(slot, interaction, resolve_target(slot));
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