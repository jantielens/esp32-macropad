#ifndef DISPLAY_FRAME_PUBLISHER_H
#define DISPLAY_FRAME_PUBLISHER_H

#include "display_driver.h"

// Owned exclusively by the LVGL task (or its mutex during external mutations).
class DisplayFramePublisher {
public:
    uint32_t interactionChanged(uint64_t now) {
        interaction.intent = DisplayIntent::Interactive;
        interaction.request_sequence = ++requestSequence;
        interaction.requested_us = now;
        return interaction.request_sequence;
    }

    bool needsPromptRender() const { return interaction.intent == DisplayIntent::Interactive; }
    const DisplayFrame& renderingFrame() const { return rendering; }

    bool renderIfNeeded(DisplayDriver& driver, lv_display_t* display, bool blocked) {
        if (!driver.coherentFrames() || blocked || !needsPromptRender()) return false;
        lv_refr_now(display);
        return true;
    }

    bool event(DisplayDriver& driver, lv_event_code_t code, uint64_t now) {
        if (!driver.coherentFrames()) return false;
        if (code == LV_EVENT_REFR_START) {
            driver.beginFrame();
            rendering = interaction;
            interaction = {};
            rendering.sequence = ++sequence;
        } else if (code == LV_EVENT_REFR_READY) {
            rendering.ready_us = now;
            driver.commitFrame(rendering);
            // A manual refresh may have raced even a no-flush generation.
            return true;
        }
        return false;
    }

private:
    DisplayFrame interaction;
    DisplayFrame rendering;
    uint32_t sequence = 0;
    uint32_t requestSequence = 0;
};

#endif
