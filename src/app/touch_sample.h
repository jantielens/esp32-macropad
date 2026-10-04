#pragma once

#include <stdint.h>

enum class TouchReadStatus : uint8_t { Fresh, Unchanged, Error };

struct TouchSample {
    TouchReadStatus status = TouchReadStatus::Fresh;
    bool pressed = false;
    uint16_t horizontal = 0;
    uint16_t vertical = 0;
};

static constexpr uint8_t TOUCH_CONTACT_CAPACITY = 5;
static constexpr uint8_t TOUCH_TRACKING_ID_COUNT = 16;

constexpr uint16_t touch_contact_id_mask(uint8_t id) {
    return id < TOUCH_TRACKING_ID_COUNT ? uint16_t(uint16_t(1) << id) : 0;
}

struct TouchContact {
    uint8_t id = 0;
    uint16_t horizontal = 0;
    uint16_t vertical = 0;
};

struct TouchSnapshot {
    TouchReadStatus status = TouchReadStatus::Fresh;
    uint8_t count = 0;
    TouchContact contacts[TOUCH_CONTACT_CAPACITY]{};
};

class TouchReadFilterState {
public:
    static constexpr uint32_t error_timeout_ms = 100;
    bool timed_out = false;
    bool newly_timed_out = false;

    bool update(TouchReadStatus status, bool released, uint32_t now) {
        timed_out = newly_timed_out = false;
        if (status == TouchReadStatus::Error) {
            if (!error_active_) { error_active_ = true; error_started_ = now; }
        } else if (status == TouchReadStatus::Fresh) {
            error_active_ = false;
            if (released) blocked_ = false;
        }
        const bool accept = status != TouchReadStatus::Error && !error_active_ && !blocked_;
        if (error_active_ && uint32_t(now - error_started_) >= error_timeout_ms) {
            timed_out = true;
            newly_timed_out = !blocked_;
            blocked_ = true;
        }
        return accept;
    }

private:
    uint32_t error_started_ = 0;
    bool error_active_ = false;
    bool blocked_ = false;
};

class TouchSnapshotFilter {
public:
    bool canceled = false;

    TouchSnapshot update(const TouchSnapshot& snapshot, uint32_t now) {
        const bool accept = state_.update(snapshot.status, !snapshot.count, now);
        if (accept && snapshot.status == TouchReadStatus::Fresh) last_ = snapshot;
        canceled = state_.newly_timed_out;
        if (state_.timed_out) last_.count = 0;
        TouchSnapshot result = last_;
        result.status = snapshot.status;
        return result;
    }

private:
    TouchSnapshot last_;
    TouchReadFilterState state_;
};

class TouchSampleFilter {
public:
    static constexpr uint32_t error_timeout_ms = TouchReadFilterState::error_timeout_ms;
    bool canceled = false;

    TouchSample update(const TouchSample& sample, uint32_t now) {
        canceled = false;
        if (state_.update(sample.status, !sample.pressed, now)) last_ = sample;
        if (state_.timed_out) {
            canceled = last_.pressed;
            last_.pressed = false;
        }
        return last_;
    }

private:
    TouchSample last_;
    TouchReadFilterState state_;
};