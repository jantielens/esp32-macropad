#pragma once

// Smoothing primitives for scale sensors (EMA + dead-band + windowed flow rate).
// Gated by HAS_SCALE so non-coffee-scale builds get an empty translation unit.

#include "board_config.h"

#if HAS_SCALE

#include <stdint.h>
#include <stddef.h>

// Preset enum
enum ScaleSmoothingPreset : uint8_t {
    SCALE_PRESET_STABLE     = 0,
    SCALE_PRESET_BALANCED   = 1,  // default
    SCALE_PRESET_RESPONSIVE = 2,
    SCALE_PRESET_COUNT
};

// Resolved smoothing parameters (expanded from preset)
struct ScaleSmoothingParams {
    float    ema_alpha;        // 0 -> stable, 1 -> raw
    float    deadband;         // grams
    uint32_t flow_window_ms;   // lookback for derivative
    float    flow_deadband;    // g/s — flow values below this snap to 0
};

// Ring buffer sample
struct WeightSample {
    float    weight;
    uint32_t ms;
};

// Smoothing state — one instance per sensor backend
static constexpr size_t   FLOW_RING_CAPACITY = 80;
static constexpr float    JUMP_THRESHOLD     = 5.0f;   // grams — always fixed
static constexpr uint32_t FLOW_UPDATE_MS     = 100;    // recalc interval — always fixed
static constexpr uint32_t FLOW_MIN_SPAN_MS   = 50;     // min span — always fixed

struct ScaleSmoothingState {
    // EMA
    float weight_ema;
    float weight_display;    // dead-band filtered
    bool  ema_primed;

    // Flow rate ring buffer
    WeightSample flow_ring[FLOW_RING_CAPACITY];
    size_t   ring_head;
    size_t   ring_count;
    uint32_t flow_last_ms;
    float    flow_rate;       // g/s (windowed derivative)
};

// Get the resolved parameters for a preset index.
const ScaleSmoothingParams& scale_smoothing_get_params(uint8_t preset_index);

// Human-readable name for a preset index ("Stable"/"Balanced"/"Responsive").
// Out-of-range indices fall back to the Balanced name. Single source of truth
// for the preset display names (used by config logging and the MCP scale tool).
const char* scale_smoothing_preset_name(uint8_t preset_index);

// Apply a preset — updates the active params. Thread-safe (called from main loop).
void scale_smoothing_apply(uint8_t preset_index);

// Get the currently active params.
const ScaleSmoothingParams& scale_smoothing_active_params();

// Process one raw calibrated sample. Updates state in-place.
void scale_smoothing_process(ScaleSmoothingState &state, float raw_calibrated, uint32_t now_ms);

// Reset smoothing state (called after tare or init).
void scale_smoothing_reset(ScaleSmoothingState &state);

#endif // HAS_SCALE
