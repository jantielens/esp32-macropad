#ifndef PAD_OVERLAY_H
#define PAD_OVERLAY_H

#include <stdint.h>
#include <string.h>

enum BtnState : uint8_t {
    BTN_STATE_ENABLED = 0,
    BTN_STATE_DISABLED = 1,
    BTN_STATE_HIDDEN = 2,
};

struct PadOverlayRect {
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
};

inline BtnState pad_overlay_resolve_state(const char* value, bool resolved,
                                          bool overlay, bool configured) {
    if (!configured) return BTN_STATE_ENABLED;
    const BtnState fallback = overlay ? BTN_STATE_HIDDEN : BTN_STATE_ENABLED;
    if (!resolved || !value || !value[0]) return fallback;
    if (strcmp(value, "enabled") == 0) return BTN_STATE_ENABLED;
    if (strcmp(value, "disabled") == 0) return BTN_STATE_DISABLED;
    if (strcmp(value, "hidden") == 0) return BTN_STATE_HIDDEN;
    return fallback;
}

inline bool pad_overlay_rect_contains(const PadOverlayRect& rect,
                                      int32_t x, int32_t y) {
    return x >= rect.x1 && x <= rect.x2 && y >= rect.y1 && y <= rect.y2;
}

inline bool pad_overlay_rects_intersect(const PadOverlayRect& first,
                                        const PadOverlayRect& second) {
    return first.x1 <= second.x2 && second.x1 <= first.x2 &&
           first.y1 <= second.y2 && second.y1 <= first.y2;
}

inline bool pad_overlay_blocks_point(BtnState state, const PadOverlayRect& rect,
                                     int32_t x, int32_t y) {
    return state != BTN_STATE_HIDDEN && pad_overlay_rect_contains(rect, x, y);
}

inline bool pad_overlay_covers_button(BtnState state, const PadOverlayRect& overlay,
                                      const PadOverlayRect& button) {
    return state != BTN_STATE_HIDDEN && pad_overlay_rects_intersect(overlay, button);
}

#endif