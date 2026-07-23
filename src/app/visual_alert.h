#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include <stdint.h>

// Pulse patterns for the full-screen visual alert overlay.
#define VA_PATTERN_BREATHE 0   // smooth eased opacity ping-pong
#define VA_PATTERN_BLINK   1   // hard on/off toggle
#define VA_PATTERN_SOLID   2   // static tint, no animation

// Defaults applied when a field is left at 0 (single source of truth for both
// the action dispatch path and the overlay module).
#define VA_DEFAULT_PERIOD_MS 800
#define VA_DEFAULT_INTENSITY 100

// Visual alert parameters (all fields pre-resolved plain values).
struct VisualAlertParams {
    uint32_t color;        // 0x00RRGGBB overlay tint
    uint8_t  pattern;      // VA_PATTERN_*
    uint16_t period_ms;    // pulse cadence (full cycle)
    uint32_t duration_ms;  // 0 = persist until stopped
    uint8_t  intensity;    // 1-100 = max overlay opacity (%)
};

// Parse pattern string ("blink", "solid", "breathe") to VA_PATTERN_*.
// Returns VA_PATTERN_BREATHE for unrecognized or empty input.
uint8_t visual_alert_pattern_from_str(const char* s);

// Raise a full-screen pulsing color alert. Replaces any active alert
// (last-write-wins). Thread-safe: callable from any task (deferred flag).
void visual_alert_show(const VisualAlertParams* params);

// Clear the active alert (if any), fading out. Thread-safe.
void visual_alert_stop();

// Process deferred show/stop. Call from main loop().
// Acquires the LVGL mutex internally for LVGL operations.
void visual_alert_loop();

#else

static inline uint8_t visual_alert_pattern_from_str(const char*) { return 0; }
static inline void visual_alert_loop() {}

#endif // HAS_DISPLAY
