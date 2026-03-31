#include "scale_smoothing.h"
#include <Arduino.h>
#include <string.h>

// ============================================================================
// Preset table
// ============================================================================
//                                    ema_alpha  deadband  flow_window_ms  flow_ema_alpha
static const ScaleSmoothingParams PRESETS[SCALE_PRESET_COUNT] = {
    { 0.15f, 0.03f, 1500, 0.15f },  // STABLE
    { 0.25f, 0.05f, 1000, 0.20f },  // BALANCED
    { 0.40f, 0.05f,  500, 0.35f },  // RESPONSIVE
};

// Active params — mutable copy of the selected preset
static ScaleSmoothingParams s_active = PRESETS[SCALE_PRESET_BALANCED];

// ============================================================================
// API
// ============================================================================

const ScaleSmoothingParams& scale_smoothing_get_params(uint8_t preset_index) {
    if (preset_index >= SCALE_PRESET_COUNT) preset_index = SCALE_PRESET_BALANCED;
    return PRESETS[preset_index];
}

void scale_smoothing_apply(uint8_t preset_index) {
    if (preset_index >= SCALE_PRESET_COUNT) preset_index = SCALE_PRESET_BALANCED;
    s_active = PRESETS[preset_index];
}

const ScaleSmoothingParams& scale_smoothing_active_params() {
    return s_active;
}

void scale_smoothing_reset(ScaleSmoothingState &state) {
    state.weight_ema     = 0.0f;
    state.weight_display = 0.0f;
    state.ema_primed     = false;
    state.ring_head      = 0;
    state.ring_count     = 0;
    state.flow_last_ms   = 0;
    state.flow_rate      = 0.0f;
    state.flow_rate_raw  = 0.0f;
    memset(state.flow_ring, 0, sizeof(state.flow_ring));
}

void scale_smoothing_process(ScaleSmoothingState &state, float raw_calibrated, uint32_t now_ms) {
    // EMA filter with jump detection
    if (!state.ema_primed) {
        state.weight_ema     = raw_calibrated;
        state.weight_display = raw_calibrated;
        state.ema_primed     = true;
    } else {
        float delta = raw_calibrated - state.weight_ema;
        if (delta > JUMP_THRESHOLD || delta < -JUMP_THRESHOLD) {
            state.weight_ema     = raw_calibrated;  // instant reset on big change
            state.weight_display = raw_calibrated;
        } else {
            state.weight_ema = s_active.ema_alpha * raw_calibrated
                             + (1.0f - s_active.ema_alpha) * state.weight_ema;
        }
    }

    // Dead-band: only update display value when EMA drifts past threshold
    float db_delta = state.weight_ema - state.weight_display;
    if (db_delta > s_active.deadband || db_delta < -s_active.deadband) {
        state.weight_display = state.weight_ema;
    }

    // Push sample into ring buffer
    state.flow_ring[state.ring_head] = { state.weight_ema, now_ms };
    state.ring_head = (state.ring_head + 1) % FLOW_RING_CAPACITY;
    if (state.ring_count < FLOW_RING_CAPACITY) state.ring_count++;

    // Recalculate flow rate at FLOW_UPDATE_MS intervals
    if (state.ring_count >= 2 && (now_ms - state.flow_last_ms) >= FLOW_UPDATE_MS) {
        state.flow_last_ms = now_ms;
        size_t newest_idx = (state.ring_head + FLOW_RING_CAPACITY - 1) % FLOW_RING_CAPACITY;
        const WeightSample &newest = state.flow_ring[newest_idx];

        // Find reference sample ~flow_window_ms ago (scan backward from newest)
        uint32_t target_ms = now_ms - s_active.flow_window_ms;
        size_t ref_idx = (state.ring_head + FLOW_RING_CAPACITY - state.ring_count) % FLOW_RING_CAPACITY;
        for (size_t i = 1; i < state.ring_count; i++) {
            size_t idx = (state.ring_head + FLOW_RING_CAPACITY - 1 - i) % FLOW_RING_CAPACITY;
            if (state.flow_ring[idx].ms <= target_ms) {
                ref_idx = idx;
                break;
            }
        }

        const WeightSample &ref = state.flow_ring[ref_idx];
        uint32_t span = newest.ms - ref.ms;
        if (span >= FLOW_MIN_SPAN_MS) {
            state.flow_rate_raw = (newest.weight - ref.weight) / ((float)span / 1000.0f);
            // Second-stage EMA on flow rate to reduce jitter
            state.flow_rate = s_active.flow_ema_alpha * state.flow_rate_raw
                            + (1.0f - s_active.flow_ema_alpha) * state.flow_rate;
        }
    }
}
