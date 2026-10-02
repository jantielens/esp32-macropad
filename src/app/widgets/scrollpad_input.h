#pragma once

#include <stdint.h>
#include <cmath>

class ScrollpadInput {
public:
    void press(int x, int y, uint32_t now = 0) {
        last_x = x;
        last_y = y;
        fraction = 0;
        velocity = 0;
        last_sample = last_motion = now;
        active = true;
    }

    int move(int x, int y, bool horizontal, float sensitivity, bool reverse, uint32_t now = 0) {
        if (!active) return 0;
        const int distance = horizontal ? x - last_x : last_y - y;
        last_x = x;
        last_y = y;
        const float travel = distance * sensitivity * (reverse ? -1.0f : 1.0f) / 20.0f;
        const uint32_t elapsed = now - last_sample;
        if (elapsed) {
            const float weight = 1 - std::exp(-static_cast<float>(elapsed) / 50);
            velocity += (travel * 1000 / elapsed - velocity) * weight;
        }
        last_sample = now;
        if (distance) last_motion = now;
        fraction += travel;
        const int steps = static_cast<int>(fraction);
        fraction -= steps;
        return steps;
    }

    float release(uint32_t now) {
        const float speed = active && now - last_motion <= 80
            ? velocity * std::exp(-static_cast<float>(now - last_sample) / 50) : 0;
        cancel();
        return speed;
    }

    void cancel() {
        active = false;
        fraction = 0;
        velocity = 0;
    }

private:
    int last_x = 0;
    int last_y = 0;
    float fraction = 0;
    float velocity = 0;
    uint32_t last_sample = 0;
    uint32_t last_motion = 0;
    bool active = false;
};