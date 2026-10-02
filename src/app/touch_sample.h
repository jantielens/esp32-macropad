#pragma once

#include <stdint.h>

enum class TouchReadStatus : uint8_t { Fresh, Unchanged, Error };

struct TouchSample {
    TouchReadStatus status = TouchReadStatus::Fresh;
    bool pressed = false;
    uint16_t horizontal = 0;
    uint16_t vertical = 0;
};

class TouchSampleFilter {
public:
    static constexpr uint32_t error_timeout_ms = 100;
    bool canceled = false;

    TouchSample update(const TouchSample& sample, uint32_t now) {
        canceled = false;
        if (sample.status == TouchReadStatus::Error) {
            if (!error_active_) { error_active_ = true; error_started_ = now; }
        } else if (sample.status == TouchReadStatus::Fresh) {
            error_active_ = false;
            if (!sample.pressed) blocked_ = false;
            if (!blocked_) last_ = sample;
        } else if (!error_active_ && !blocked_) {
            last_ = sample;
        }
        if (error_active_ && uint32_t(now - error_started_) >= error_timeout_ms) {
            canceled = last_.pressed;
            last_.pressed = false;
            blocked_ = true;
        }
        return last_;
    }

private:
    TouchSample last_;
    uint32_t error_started_ = 0;
    bool error_active_ = false;
    bool blocked_ = false;
};