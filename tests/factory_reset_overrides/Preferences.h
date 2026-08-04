#pragma once

#include <cstddef>
#include <cstdint>

class Preferences {
public:
    bool begin(const char*, bool) { return true; }
    void end() {}
    uint32_t getUInt(const char*, uint32_t value = 0) const { return value; }
    uint16_t getUShort(const char*, uint16_t value = 0) const { return value; }
    uint8_t getUChar(const char*, uint8_t value = 0) const { return value; }
    bool getBool(const char*, bool value = false) const { return value; }
    size_t getString(const char*, char* output, size_t length) const {
        if (length) output[0] = '\0';
        return 0;
    }
    bool putString(const char*, const char*) { return true; }
    bool putUShort(const char*, uint16_t) { return true; }
    bool putUChar(const char*, uint8_t) { return true; }
    bool putUInt(const char*, uint32_t) { return true; }
    bool putBool(const char*, bool) { return true; }
};