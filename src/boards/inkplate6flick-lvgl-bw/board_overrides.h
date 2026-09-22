#pragma once

// Reuse the experimental LVGL Inkplate hardware and feature selection.
#include "../inkplate6flick-lvgl/board_overrides.h"

// Use the Inkplate 6FLICK 1-bit B/W mode and its partial-update waveform.
#undef INKPLATE_LVGL_DEFAULT_MODE
#define INKPLATE_LVGL_DEFAULT_MODE INKPLATE_LVGL_MODE_BW
// Partial updates make current binding values useful without full waveforms.
#undef DISPLAY_BINDING_REFRESH_INTERVAL_MS
#define DISPLAY_BINDING_REFRESH_INTERVAL_MS 1000
// A short floor prevents action-driven partial-update bursts.
#undef INKPLATE_MIN_REFRESH_MS
#define INKPLATE_MIN_REFRESH_MS 250