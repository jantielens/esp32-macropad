#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

class Preferences {
public:
    static inline bool begin_ok = true;
    static inline bool write_ok = true;
    static inline uint8_t saved_volume = 0;
    static inline unsigned volume_writes = 0;
    bool begin(const char*, bool) { return begin_ok; }
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
    bool putUChar(const char* key, uint8_t value) {
        if (!write_ok) return false;
        if (std::strcmp(key, "audio_vol") == 0) {
            saved_volume = value;
            ++volume_writes;
        }
        return true;
    }
    bool putUInt(const char*, uint32_t) { return true; }
    bool putBool(const char*, bool) { return true; }
};