#pragma once

#include <stdint.h>
#include <cmath>
#include "scrollpad_input.h"
#include "../mouse_hid_buttons.h"

class MousepadInput {
public:
    static constexpr float default_movement_threshold = 3.0f;
    static constexpr float max_movement_threshold = 12.0f;
    static constexpr uint32_t tap_duration_ms = 250;
    static constexpr bool horizontal_axis_on_tie = false;

    struct ButtonZones {
        int width = 0;
        int height = 0;

        int top() const { return height - height / 5; }
        int split() const { return width / 2; }
        int left(bool right) const { return right ? split() : 0; }
        int right(bool right) const { return (right ? width : split()) - 1; }

        uint8_t mask_at(int x, int y) const {
            if (x < 0 || x >= width || y < top() || y >= height) return 0;
            return x < split() ? mouse_hid_buttons::left : mouse_hid_buttons::right;
        }
    };

    struct Output {
        int dx = 0;
        int dy = 0;
        int wheel = 0;
        int pan = 0;
        float velocity = 0;
        bool horizontal = false;
        bool click = false;
        bool hold_start = false;
        bool hold_end = false;
        uint8_t button_mask = mouse_hid_buttons::left;
    };

    void configure_buttons(bool enabled, int width, int height) {
        button_zones = {enabled ? width : 0, height};
    }

    bool button_held() const { return mode == Mode::ButtonHeld; }
    uint8_t held_button() const { return button_held() ? button_mask : 0; }

    Output contact_press(uint8_t id, int x, int y, uint32_t now) {
        Output output;
        const uint8_t zone_mask = button_zones.mask_at(x, y);
        const bool in_buttons = zone_mask != 0;
        if (in_buttons && (mode == Mode::Idle || mode == Mode::Pointer)) {
            pointer_active = mode == Mode::Pointer;
            button_id = id;
            button_mask = zone_mask;
            mode = Mode::ButtonHeld;
            output.hold_start = true;
            output.button_mask = button_mask;
        } else if (mode == Mode::ButtonHeld) {
            if (!in_buttons && !pointer_active && id != button_id) {
                first = {id, x, y};
                pointer_active = true;
                press(x, y, now);
            }
        } else if (mode == Mode::Idle) {
            first = {id, x, y};
            mode = Mode::Pointer;
            press(x, y, now);
        } else if (mode == Mode::Pointer && id != first.id) {
            second = {id, x, y};
            mode = Mode::Scrolling;
            active = false;
            axis_locked = false;
            baseline_x = (first.x + second.x) / 2;
            baseline_y = (first.y + second.y) / 2;
            scrolling.press(baseline_x, baseline_y, now);
        }
        return output;
    }

    void contact_position(uint8_t id, int x, int y) {
        Contact* contact = id == first.id ? &first : id == second.id ? &second : nullptr;
        if (contact) { contact->x = x; contact->y = y; }
    }

    Output contact_move(uint8_t id, int x, int y, uint32_t now,
                        float sensitivity, float acceleration, float threshold) {
        Output output;
        if ((mode == Mode::Pointer || (mode == Mode::ButtonHeld && pointer_active)) && id == first.id) {
            first.x = x;
            first.y = y;
            move(x, y, sensitivity, output.dx, output.dy, now, acceleration, threshold);
        } else if (mode == Mode::Scrolling) {
            Contact* contact = id == first.id ? &first : id == second.id ? &second : nullptr;
            if (!contact) return output;
            contact->x = x;
            contact->y = y;
        }
        return output;
    }

    Output scroll_sample(uint32_t now, float sensitivity, float threshold, bool reverse) {
        Output output;
        if (mode != Mode::Scrolling) return output;
        const int x = (first.x + second.x) / 2;
        const int y = (first.y + second.y) / 2;
        if (!axis_locked) {
            const int travel_x = x - baseline_x;
            const int travel_y = y - baseline_y;
            if (travel_x * travel_x + travel_y * travel_y <= threshold * threshold) return output;
            horizontal = std::abs(travel_x) == std::abs(travel_y) ? horizontal_axis_on_tie :
                         std::abs(travel_x) > std::abs(travel_y);
            axis_locked = true;
        }
        const int steps = scrolling.move(x, y, horizontal, sensitivity, reverse, now);
        output.wheel = horizontal ? 0 : steps;
        output.pan = horizontal ? steps : 0;
        return output;
    }

    Output contact_release(uint8_t id, uint32_t now) {
        Output output;
        if (mode == Mode::ButtonHeld && id == button_id) {
            output.hold_end = true;
            if (pointer_active) {
                mode = Mode::Pointer;
                press(first.x, first.y, now);
                moved = true;
            } else mode = Mode::Waiting;
        } else if (mode == Mode::ButtonHeld && pointer_active && id == first.id) {
            pointer_active = active = false;
        } else if (mode == Mode::Pointer && id == first.id) {
            output.click = release(now);
            mode = Mode::Waiting;
        } else if (mode == Mode::Scrolling && (id == first.id || id == second.id)) {
            output.velocity = scrolling.release(now);
            output.horizontal = horizontal;
            mode = Mode::Waiting;
        }
        return output;
    }

    void end_session() {
        mode = Mode::Idle;
        active = false;
        pointer_active = false;
        scrolling.cancel();
    }

    void press(int x, int y, uint32_t now) {
        start_x = last_x = x;
        start_y = last_y = y;
        pressed_at = now;
        last_at = now;
        fraction_x = fraction_y = 0;
        active = true;
        moved = false;
    }

    void move(int x, int y, float sensitivity, int& dx, int& dy,
              uint32_t now = 0, float acceleration = 0,
              float movement_threshold = default_movement_threshold) {
        dx = dy = 0;
        if (!active) return;
        if (!moved) {
            const int distance_x = x - start_x;
            const int distance_y = y - start_y;
            if (distance_x * distance_x + distance_y * distance_y <= movement_threshold * movement_threshold) return;
            moved = true;
        }
        float gain = 1;
        const uint32_t elapsed = now - last_at;
        if (acceleration > 0 && elapsed) {
            const float distance_x = x - last_x;
            const float distance_y = y - last_y;
            const float speed = std::sqrt(distance_x * distance_x + distance_y * distance_y) * 1000 / elapsed;
            const float excess = speed > 120 ? speed - 120 : 0;
            gain += acceleration * 0.5f * excess / (excess + 600);
        }
        last_at = now;
        fraction_x += (x - last_x) * sensitivity * gain;
        fraction_y += (y - last_y) * sensitivity * gain;
        last_x = x;
        last_y = y;
        dx = static_cast<int>(fraction_x);
        dy = static_cast<int>(fraction_y);
        fraction_x -= dx;
        fraction_y -= dy;
    }

    bool release(uint32_t now) {
        const bool click = active && !moved && now - pressed_at <= tap_duration_ms;
        active = false;
        return click;
    }

    void cancel() { end_session(); }

private:
    enum class Mode : uint8_t { Idle, Pointer, Scrolling, Waiting, ButtonHeld };
    struct Contact { uint8_t id = 0; int x = 0; int y = 0; };
    Contact first;
    Contact second;
    Mode mode = Mode::Idle;
    ScrollpadInput scrolling;
    int baseline_x = 0;
    int baseline_y = 0;
    bool axis_locked = false;
    bool horizontal = false;
    int start_x = 0;
    int start_y = 0;
    int last_x = 0;
    int last_y = 0;
    uint32_t pressed_at = 0;
    uint32_t last_at = 0;
    float fraction_x = 0;
    float fraction_y = 0;
    bool active = false;
    bool moved = false;
    ButtonZones button_zones;
    uint8_t button_id = 0;
    uint8_t button_mask = 0;
    bool pointer_active = false;
};