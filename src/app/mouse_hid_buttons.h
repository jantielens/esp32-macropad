#pragma once

#include <stdint.h>

namespace mouse_hid_buttons {
constexpr uint8_t left = 1;
constexpr uint8_t right = 2;
constexpr uint8_t middle = 4;

inline bool valid(uint8_t buttons) {
    return buttons == left || buttons == right || buttons == middle;
}
}