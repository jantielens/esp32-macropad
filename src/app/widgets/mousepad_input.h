#pragma once

#include <stdint.h>
#include <cmath>
#include "scrollpad_input.h"

class MousepadInput {
public:
    static constexpr float default_movement_threshold = 3.0f;
    static constexpr float max_movement_threshold = 12.0f;
    static constexpr uint32_t tap_duration_ms = 250;
    static constexpr uint32_t tap_follow_ms = 300;
    static constexpr bool horizontal_axis_on_tie = false;

    struct Output {
        int dx = 0;
        int dy = 0;
        int wheel = 0;
        int pan = 0;
        float velocity = 0;
        bool horizontal = false;
        bool click = false;
        bool drag_start = false;
        bool drag_end = false;
    };

    void contact_press(uint8_t id, int x, int y, uint32_t now) {
        if (mode == Mode::Idle) {
            drag_candidate = tap_pending && uint32_t(now - tapped_at) <= tap_follow_ms;
            tap_pending = false;
            first = {id, x, y};
            mode = Mode::Pointer;
            press(x, y, now);
        } else if (mode == Mode::Pointer && id != first.id) {
            second = {id, x, y};
            mode = Mode::Scrolling;
            drag_candidate = tap_pending = false;
            active = false;
            axis_locked = false;
            baseline_x = (first.x + second.x) / 2;
            baseline_y = (first.y + second.y) / 2;
            scrolling.press(baseline_x, baseline_y, now);
        }
    }

    void contact_position(uint8_t id, int x, int y) {
        Contact* contact = id == first.id ? &first : id == second.id ? &second : nullptr;
        if (contact) { contact->x = x; contact->y = y; }
    }

    Output contact_move(uint8_t id, int x, int y, uint32_t now,
                        float sensitivity, float acceleration, float threshold) {
        Output output;
        if ((mode == Mode::Pointer || mode == Mode::Dragging) && id == first.id) {
            first.x = x;
            first.y = y;
            move(x, y, sensitivity, output.dx, output.dy, now, acceleration, threshold);
            if (moved && drag_candidate && mode == Mode::Pointer) {
                mode = Mode::Dragging;
                output.drag_start = true;
            }
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
        if (mode == Mode::Pointer && id == first.id) {
            output.click = release(now);
            tap_pending = output.click && !drag_candidate;
            tapped_at = now;
            mode = Mode::Waiting;
        } else if (mode == Mode::Dragging && id == first.id) {
            output.drag_end = true;
            active = false;
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

    void cancel() { end_session(); tap_pending = drag_candidate = false; }

private:
    enum class Mode : uint8_t { Idle, Pointer, Scrolling, Dragging, Waiting };
    struct Contact { uint8_t id = 0; int x = 0; int y = 0; };
    Contact first;
    Contact second;
    Mode mode = Mode::Idle;
    ScrollpadInput scrolling;
    int baseline_x = 0;
    int baseline_y = 0;
    bool axis_locked = false;
    bool horizontal = false;
    uint32_t tapped_at = 0;
    bool tap_pending = false;
    bool drag_candidate = false;
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
};