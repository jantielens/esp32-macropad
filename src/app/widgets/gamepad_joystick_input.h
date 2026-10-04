#pragma once

#include <cmath>
#include <stdint.h>
#include "../gamepad_hid_state.h"

struct GamepadStickPosition {
    int16_t horizontal = 0;
    int16_t vertical = 0;
};

class GamepadJoystickInput {
public:
    bool active = false;
    float center_x = 0;
    float center_y = 0;
    float radius = 0;
    float base_radius = 0;
    float thumb_radius = 0;
    float thumb_x = 0;
    float thumb_y = 0;

    bool press(float horizontal, float vertical, float left, float top,
               float width, float height, bool floating) {
        cancel();
        if (width < 16 || height < 16) return false;
        const float diameter = std::floor(std::fmin(width, height));
        base_radius = diameter * 0.5f;
        thumb_radius = std::fmax(6.0f, std::floor(diameter / 3)) * 0.5f;
        radius = base_radius - thumb_radius;
        center_x = left + width * 0.5f;
        center_y = top + height * 0.5f;
        if (floating) {
            center_x = std::fmax(left + base_radius, std::fmin(left + width - base_radius, horizontal));
            center_y = std::fmax(top + base_radius, std::fmin(top + height - base_radius, vertical));
        }
        thumb_x = center_x;
        thumb_y = center_y;
        active = true;
        return true;
    }

    GamepadStickPosition move(float horizontal, float vertical, float dead_zone,
                              bool invert_x, bool invert_y) {
        GamepadStickPosition result;
        if (!active || radius <= 0) return result;
        const float delta_x = (horizontal - center_x) / radius;
        const float delta_y = (vertical - center_y) / radius;
        const float distance = std::sqrt(delta_x * delta_x + delta_y * delta_y);
        const float bounded = std::fmin(distance, 1.0f);
        thumb_x = center_x + (distance > 0 ? delta_x / distance * bounded * radius : 0);
        thumb_y = center_y + (distance > 0 ? delta_y / distance * bounded * radius : 0);
        if (!std::isfinite(dead_zone)) dead_zone = 0.1f;
        dead_zone = std::fmax(0.0f, std::fmin(0.9f, dead_zone));
        if (distance <= dead_zone || distance == 0) return result;
        const float deflection = (bounded - dead_zone) / (1.0f - dead_zone);
        result.horizontal = int16_t(std::lround(delta_x / distance * deflection * gamepad_protocol::axis_max * (invert_x ? -1 : 1)));
        result.vertical = int16_t(std::lround(delta_y / distance * deflection * gamepad_protocol::axis_max * (invert_y ? -1 : 1)));
        return result;
    }

    void cancel() { active = false; }
};