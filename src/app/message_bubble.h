#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include <stdint.h>

// Notification parameters (all fields are pre-resolved plain values).
struct MessageBubbleParams {
    char text[128];          // message text (already resolved)
    uint16_t duration_ms;    // 0 = persistent
    uint32_t text_color;     // 0x00RRGGBB
    uint32_t bg_color;       // 0x00RRGGBB
    uint32_t border_color;   // 0x00RRGGBB
    bool has_border;         // true if border_color was specified
    uint8_t opacity;         // 0-100 (%)
    uint8_t font_size;       // 0 = auto
    uint8_t location;        // NOTIFY_LOC_*
};

#define NOTIFY_LOC_BOTTOM  0
#define NOTIFY_LOC_CENTER  1
#define NOTIFY_LOC_TOP     2

// Parse location string ("top", "center", "bottom") to NOTIFY_LOC_* constant.
// Returns NOTIFY_LOC_BOTTOM for unrecognized or empty input.
uint8_t notify_location_from_str(const char* loc);

// Show a message bubble overlay. Replaces any currently visible bubble.
// Empty text dismisses the current bubble.
// Thread-safe: can be called from any task (uses deferred flag pattern).
void message_bubble_show(const MessageBubbleParams* params);

// Dismiss the current bubble immediately (if any). Thread-safe.
void message_bubble_dismiss();

// Process deferred show/dismiss. Call from main loop().
// Acquires LVGL mutex internally for LVGL operations.
void message_bubble_loop();

#else

static inline void message_bubble_loop() {}

#endif // HAS_DISPLAY
