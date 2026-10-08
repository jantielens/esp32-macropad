#ifndef DISPLAY_GEOMETRY_H
#define DISPLAY_GEOMETRY_H

#include "board_config.h"
#include <stdint.h>

struct DisplayContentRect {
    uint16_t x, y, w, h;
};

static inline DisplayContentRect display_safe_content_rect(uint16_t width, uint16_t height) {
#if HAS_M5STACK_STOPWATCH
    const uint16_t diameter = width < height ? width : height;
    // Floor(diameter / sqrt(2)) conservatively, so every corner is visible.
    const uint16_t side = uint32_t(diameter) * 707 / 1000;
    return {uint16_t((width - side) / 2), uint16_t((height - side) / 2), side, side};
#else
    return {0, 0, width, height};
#endif
}

#endif
