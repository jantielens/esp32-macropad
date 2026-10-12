#ifndef DISPLAY_GEOMETRY_H
#define DISPLAY_GEOMETRY_H

#include "board_config.h"
#include <stdint.h>

struct DisplayContentRect {
    uint16_t x, y, w, h;
};

static inline DisplayContentRect display_safe_content_rect(uint16_t width, uint16_t height) {
    return {0, 0, width, height};
}

#endif
