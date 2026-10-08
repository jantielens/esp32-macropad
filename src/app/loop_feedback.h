#pragma once

#include "tone_alert_overlay.h"

struct LoopFeedbackMailbox {
    uint32_t command_id = 0;
    bool protected_loop = false;
    bool pending = false;
    ToneAlertOverlay tone = {};

    uint32_t replace(bool loop) {
        ++command_id;
        protected_loop = loop;
        pending = false;
        return command_id;
    }

    bool submit(const ToneAlertOverlay& feedback) {
        if (!protected_loop) return false;
        tone = feedback;
        pending = true;
        return true;
    }

    bool matches(uint32_t owner) const {
        return protected_loop && command_id == owner;
    }

    bool take(uint32_t owner, ToneAlertOverlay* output) {
        if (!matches(owner) || !pending) return false;
        *output = tone;
        pending = false;
        return true;
    }

    void finish(uint32_t owner) {
        if (command_id != owner) return;
        protected_loop = false;
        pending = false;
    }
};

static inline void loop_feedback_mix(ToneAlertOverlay* feedback, int16_t* frames,
                                     size_t count, uint32_t sample_rate) {
    if (!feedback->active) return;
    for (size_t frame = 0; frame < count && feedback->active; ++frame) {
        frames[frame * 2] = (int16_t)((int32_t)frames[frame * 2] * 3 / 4);
        frames[frame * 2 + 1] = (int16_t)((int32_t)frames[frame * 2 + 1] * 3 / 4);
        tone_alert_overlay_mix(feedback, frames + frame * 2, 1, sample_rate);
    }
}