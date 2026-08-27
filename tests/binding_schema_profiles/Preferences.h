#pragma once

#include <stdint.h>

class Preferences {
public:
    bool begin(const char*, bool) { return true; }
    uint32_t getUInt(const char*, uint32_t value = 0) { return value; }
    void putUInt(const char*, uint32_t) {}
    uint16_t getUShort(const char*, uint16_t value = 0) { return value; }
    void putUShort(const char*, uint16_t) {}
};
