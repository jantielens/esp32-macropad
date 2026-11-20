#pragma once

#include <stdint.h>

#define DISPLAY_MANAGER_H
uint8_t display_manager_get_backlight_brightness();

class String {};
struct ListHealthEsp {
    const char* getChipModel() const { return "ESP32-P4"; }
};
inline ListHealthEsp ESP;
