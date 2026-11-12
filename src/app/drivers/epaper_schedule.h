#ifndef MACROPAD_EPAPER_SCHEDULE_H
#define MACROPAD_EPAPER_SCHEDULE_H

#include "../display_frame.h"

// All state belongs to the driver's framebuffer mutex.
struct EpaperSchedule {
    bool rendering = false;
    bool changed = false;
    DisplayFrame frame;

    void begin() { rendering = true; changed = false; }

    void commit(const DisplayFrame& completed) {
        rendering = false;
        if (!changed) {
            frame.sequence = completed.sequence;
            frame.ready_us = completed.ready_us;
            return;
        }
        const bool retainInteraction = frame.intent == DisplayIntent::Interactive;
        const uint64_t requested = frame.requested_us;
        const uint32_t requestSequence = frame.request_sequence;
        frame = completed;
        if (retainInteraction && completed.intent == DisplayIntent::Background) {
            frame.intent = DisplayIntent::Interactive;
            frame.requested_us = requested;
            frame.request_sequence = requestSequence;
        }
    }

    void consume() { frame = {}; }

    uint32_t wait(uint32_t now, bool baseline, bool full, uint32_t lastRefresh,
                  uint32_t interval, uint32_t lastChange, uint32_t firstPending,
                  uint32_t settle) const {
        if (frame.intent == DisplayIntent::Interactive) return 0;
        const uint32_t cadence = baseline && now - lastRefresh < interval
            ? interval - (now - lastRefresh) : 0;
        const uint32_t quiet = !full && now - lastChange < settle ? settle - (now - lastChange) : 0;
        const uint32_t cap = now - firstPending < 500 ? 500 - (now - firstPending) : 0;
        const uint32_t settling = quiet < cap ? quiet : cap;
        return cadence > settling ? cadence : settling;
    }
};

#endif
