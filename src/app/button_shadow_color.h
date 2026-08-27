#pragma once

#include <stdint.h>

static inline uint32_t button_shadow_darken_color(uint32_t color, uint8_t percent) {
    const uint16_t factor = 100 - percent;
    const uint8_t red = ((color >> 16) & 0xFF) * factor / 100;
    const uint8_t green = ((color >> 8) & 0xFF) * factor / 100;
    const uint8_t blue = (color & 0xFF) * factor / 100;
    return ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
}
