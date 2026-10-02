#pragma once

#include <stdint.h>
#include <cmath>

class MousepadInput {
public:
    static constexpr float default_movement_threshold = 3.0f;
    static constexpr float max_movement_threshold = 12.0f;

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
        const bool click = active && !moved && now - pressed_at <= 250;
        cancel();
        return click;
    }

    void cancel() { active = false; }

private:
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