#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static constexpr size_t TONE_ALERT_MAX_SEGMENTS = 24;
static constexpr size_t TONE_ALERT_PATTERN_MAX_LEN = 128;
static constexpr float TONE_ALERT_TWO_PI = 6.28318530717958647692f;

struct ToneAlertSegment {
    uint16_t frequency_hz;
    uint32_t frame_count;
};

struct ToneAlertOverlay {
    ToneAlertSegment segments[TONE_ALERT_MAX_SEGMENTS];
    uint8_t segment_count;
    uint8_t segment_index;
    uint32_t frame_index;
    float phase;
    float phase_increment;
    float gain;
    bool loop;
    bool active;
};

static inline void tone_alert_overlay_stop(ToneAlertOverlay* overlay) {
    overlay->active = false;
}

static inline bool tone_alert_overlay_start(ToneAlertOverlay* overlay, const char* pattern,
                                            bool loop, uint32_t sample_rate, float gain = 1.0f) {
    ToneAlertOverlay next = {};
    next.loop = loop;

    char buffer[TONE_ALERT_PATTERN_MAX_LEN] = {};
    if (!pattern || !pattern[0]) {
        strncpy(buffer, "1000:200", sizeof(buffer) - 1);
    } else {
        strncpy(buffer, pattern, sizeof(buffer) - 1);
    }

    char* saveptr = nullptr;
    for (char* token = strtok_r(buffer, " ", &saveptr); token;
         token = strtok_r(nullptr, " ", &saveptr)) {
        if (next.segment_count == TONE_ALERT_MAX_SEGMENTS) return false;
        char* colon = strchr(token, ':');
        const uint16_t frequency_hz = colon ? (uint16_t)atoi(token) : 0;
        const uint16_t duration_ms = (uint16_t)atoi(colon ? colon + 1 : token);
        if (duration_ms == 0 || duration_ms > 10000) continue;
        ToneAlertSegment& segment = next.segments[next.segment_count++];
        segment.frequency_hz = frequency_hz;
        segment.frame_count = (uint32_t)sample_rate * duration_ms / 1000;
    }
    if (next.segment_count == 0) return false;

    next.phase_increment = TONE_ALERT_TWO_PI * next.segments[0].frequency_hz / sample_rate;
    next.gain = gain;
    next.active = true;
    *overlay = next;
    return true;
}

static inline void tone_alert_overlay_advance(ToneAlertOverlay* overlay,
                                              uint32_t sample_rate) {
    overlay->segment_index++;
    if (overlay->segment_index == overlay->segment_count) {
        if (!overlay->loop) {
            overlay->active = false;
            return;
        }
        overlay->segment_index = 0;
    }
    overlay->frame_index = 0;
    overlay->phase = 0.0f;
    overlay->phase_increment = TONE_ALERT_TWO_PI *
        overlay->segments[overlay->segment_index].frequency_hz / sample_rate;
}

static inline void tone_alert_overlay_mix(ToneAlertOverlay* overlay, int16_t* frames,
                                          size_t frame_count, uint32_t sample_rate) {
    if (!overlay->active) return;

    for (size_t frame = 0; frame < frame_count && overlay->active; ++frame) {
        const ToneAlertSegment& segment = overlay->segments[overlay->segment_index];
        int32_t tone = 0;
        if (segment.frequency_hz != 0) {
            const uint32_t fade_frames = segment.frame_count > 400 ? 200 : segment.frame_count / 4;
            float envelope = 1.0f;
            if (fade_frames > 0 && overlay->frame_index < fade_frames) {
                envelope = (float)overlay->frame_index / fade_frames;
            } else if (fade_frames > 0 && overlay->frame_index >= segment.frame_count - fade_frames) {
                envelope = (float)(segment.frame_count - overlay->frame_index) / fade_frames;
            }
            tone = (int32_t)(sinf(overlay->phase) * 32767.0f * envelope * overlay->gain);
            overlay->phase += overlay->phase_increment;
            if (overlay->phase >= TONE_ALERT_TWO_PI) overlay->phase -= TONE_ALERT_TWO_PI;
        }

        const int32_t left = (int32_t)frames[frame * 2] + tone;
        const int32_t right = (int32_t)frames[frame * 2 + 1] + tone;
        frames[frame * 2] = left > INT16_MAX ? INT16_MAX : (left < INT16_MIN ? INT16_MIN : (int16_t)left);
        frames[frame * 2 + 1] = right > INT16_MAX ? INT16_MAX : (right < INT16_MIN ? INT16_MIN : (int16_t)right);

        if (++overlay->frame_index == segment.frame_count) {
            tone_alert_overlay_advance(overlay, sample_rate);
        }
    }
}