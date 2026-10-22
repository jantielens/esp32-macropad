#pragma once

#include <stdint.h>

static inline int8_t ft6336_virtual_button_at(uint16_t x, uint16_t y) {
    if (y < 240 || y >= 280) return -1;
    if (x >= 10 && x < 120) return 0;
    if (x >= 130 && x < 200) return 1;
    if (x >= 230 && x < 310) return 2;
    return -1;
}