#pragma once

#include "../board_config.h"
#include <stdint.h>

#if HAS_LVGL_EPAPER
constexpr uint8_t MONOCHROME_THRESHOLD = 128;

enum MonochromeConversion : uint8_t {
    MONOCHROME_CONVERSION_THRESHOLD,
    MONOCHROME_CONVERSION_BAYER,
    MONOCHROME_CONVERSION_BLUE_NOISE,
};

inline bool monochrome_is_white(uint8_t luminance, int16_t x = 0, int16_t y = 0,
        uint8_t conversion = MONOCHROME_CONVERSION_THRESHOLD, uint8_t threshold = MONOCHROME_THRESHOLD) {
    static constexpr uint8_t bayer[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5},
    };
    static constexpr uint8_t blueNoise[64 * 64] = {
#include "blue_noise_64.h"
    };
    if (conversion == MONOCHROME_CONVERSION_BLUE_NOISE) {
        return luminance == 255 || luminance > blueNoise[(y & 63) * 64 + (x & 63)];
    }
    if (conversion == MONOCHROME_CONVERSION_BAYER) threshold = bayer[y & 3][x & 3] * 16 + 8;
    return luminance >= threshold;
}
#endif