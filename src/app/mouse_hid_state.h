#pragma once

#include <stdint.h>
#include <cmath>
#include "mouse_hid_buttons.h"

struct MouseHidReport {
    int8_t dx = 0;
    int8_t dy = 0;
    int8_t wheel = 0;
    int8_t pan = 0;
    uint8_t buttons = 0;
    uint32_t generation = 0;
    uint32_t scroll_generation = 0;

    bool can_submit(uint32_t expected_epoch, uint32_t live_epoch,
                    uint32_t expected_ota_epoch, uint32_t live_ota_epoch,
                    bool ota_active) const {
        const bool neutral = !(dx || dy || wheel || pan || buttons);
        return expected_epoch == live_epoch &&
               (neutral || (expected_ota_epoch == live_ota_epoch && !ota_active));
    }
};

struct MouseHidInertiaTick {
    uint32_t revision = 0;
    uint32_t at = 0;
    float velocity = 0;
    float fraction = 0;
    int wheel = 0;
    int pan = 0;
    bool valid = false;
};

class MouseHidState {
public:
    static constexpr uint8_t click_capacity = 8;

    void reset() {
        pending_x = pending_y = 0;
        pending_wheel = pending_pan = 0;
        coast_velocity = coast_fraction = 0;
        ++coast_revision;
        clicks = 0;
        click_head = 0;
        release_pending = true;
        ++generation;
    }

    void move(int dx, int dy) {
        pending_x = bound(pending_x + dx, 32767);
        pending_y = bound(pending_y + dy, 32767);
    }

    void scroll(int wheel, int pan) {
        pending_wheel = bound(pending_wheel + wheel, 32767);
        pending_pan = bound(pending_pan + pan, 32767);
    }

    void stop_scroll() {
        pending_wheel = pending_pan = 0;
        coast_velocity = coast_fraction = 0;
        ++coast_revision;
        ++scroll_generation;
    }

    void start_inertia(float velocity, float inertia, bool horizontal, uint32_t now) {
        coast_velocity = coast_fraction = 0;
        ++coast_revision;
        if (!std::isfinite(velocity) || !std::isfinite(inertia) ||
            inertia <= 0 || std::fabs(velocity) < 2) return;
        coast_velocity = velocity < -40 ? -40 : velocity > 40 ? 40 : velocity;
        coast_decay_ms = 80 + (inertia > 5 ? 5 : inertia) * 120;
        coast_horizontal = horizontal;
        coast_started = coast_last_tick = now;
    }

    MouseHidInertiaTick inertia_tick(uint32_t now) const {
        MouseHidInertiaTick result;
        if (!coast_velocity) return result;
        result.valid = true;
        result.revision = coast_revision;
        result.at = now;
        const uint32_t elapsed = now - coast_last_tick;
        if (elapsed > 100 || now - coast_started >= 3000) {
            return result;
        }
        const float decay = std::exp(-static_cast<float>(elapsed) / coast_decay_ms);
        result.fraction = coast_fraction + coast_velocity * coast_decay_ms / 1000 * (1 - decay);
        result.velocity = coast_velocity * decay;
        const int steps = static_cast<int>(result.fraction);
        result.fraction -= steps;
        result.wheel = coast_horizontal ? 0 : steps;
        result.pan = coast_horizontal ? steps : 0;
        if (std::fabs(result.velocity) < 0.5f) result.velocity = result.fraction = 0;
        return result;
    }

    void apply_inertia(const MouseHidInertiaTick& result) {
        if (!result.valid || result.revision != coast_revision) return;
        coast_last_tick = result.at;
        coast_velocity = result.velocity;
        coast_fraction = result.fraction;
        ++coast_revision;
        scroll(result.wheel, result.pan);
    }

    void tick(uint32_t now) {
        apply_inertia(inertia_tick(now));
    }

    bool click(uint8_t buttons = mouse_hid_buttons::left) {
        if (!mouse_hid_buttons::valid(buttons) || clicks == click_capacity) return false;
        click_buttons[(click_head + clicks) % click_capacity] = buttons;
        ++clicks;
        return true;
    }

    bool next(MouseHidReport& report) const {
        report = {};
        report.generation = generation;
        report.scroll_generation = scroll_generation;
        if (release_pending) return true;
        report.dx = static_cast<int8_t>(bound(pending_x, 127));
        report.dy = static_cast<int8_t>(bound(pending_y, 127));
        report.wheel = static_cast<int8_t>(bound(pending_wheel, 127));
        report.pan = static_cast<int8_t>(bound(pending_pan, 127));
        if (report.dx || report.dy || report.wheel || report.pan) return true;
        if (clicks) {
            report.buttons = click_buttons[click_head];
            return true;
        }
        return false;
    }

    void acknowledge(const MouseHidReport& report) {
        if (report.generation != generation) return;
        if (release_pending) {
            release_pending = false;
        } else if (report.buttons) {
            click_head = (click_head + 1) % click_capacity;
            --clicks;
            release_pending = true;
        } else {
            pending_x -= report.dx;
            pending_y -= report.dy;
            if (report.scroll_generation == scroll_generation) {
                pending_wheel -= report.wheel;
                pending_pan -= report.pan;
            }
        }
    }

private:
    static int bound(int value, int limit) {
        return value < -limit ? -limit : value > limit ? limit : value;
    }
    int pending_x = 0;
    int pending_y = 0;
    int pending_wheel = 0;
    int pending_pan = 0;
    float coast_velocity = 0;
    float coast_fraction = 0;
    float coast_decay_ms = 0;
    uint32_t coast_started = 0;
    uint32_t coast_last_tick = 0;
    uint32_t coast_revision = 0;
    uint32_t scroll_generation = 0;
    bool coast_horizontal = false;
    uint8_t click_buttons[click_capacity] = {};
    uint8_t click_head = 0;
    uint8_t clicks = 0;
    bool release_pending = true;
    uint32_t generation = 0;
};