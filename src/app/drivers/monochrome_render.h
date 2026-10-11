#pragma once

#include <stdint.h>

constexpr uint8_t MONOCHROME_THRESHOLD = 128;

inline bool monochrome_is_white(uint8_t luminance) {
    return luminance >= MONOCHROME_THRESHOLD;
}