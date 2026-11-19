#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

class Preferences {
public:
    static inline bool begin_ok = true;
    static inline bool write_ok = true;
    static inline uint8_t saved_volume = 0;
    static inline unsigned volume_writes = 0;
    static inline std::map<std::string, std::string> strings;
    static inline uint32_t magic = 0;
    static inline int8_t mic_gain = 0;
    static inline bool mic_rumble = false;
    static inline bool mic_gain_present = false;
    static inline bool mic_rumble_present = false;
    static inline bool mic_write_ok = true;
    bool begin(const char*, bool) { return begin_ok; }
    void end() {}
    uint32_t getUInt(const char* key, uint32_t value = 0) const {
        return std::strcmp(key, "magic") == 0 && magic ? magic : value;
    }
    uint16_t getUShort(const char*, uint16_t value = 0) const { return value; }
    uint8_t getUChar(const char*, uint8_t value = 0) const { return value; }
    int8_t getChar(const char* key, int8_t value = 0) const {
        return std::strcmp(key, "mic_gain") == 0 && mic_gain_present ? mic_gain : value;
    }
    bool getBool(const char* key, bool value = false) const {
        return std::strcmp(key, "mic_rumble") == 0 && mic_rumble_present ? mic_rumble : value;
    }
    size_t getString(const char* key, char* output, size_t length) const {
        const auto it = strings.find(key);
        if (it == strings.end() || length <= it->second.size()) {
            if (length) output[0] = '\0';
            return 0;
        }
        std::memcpy(output, it->second.c_str(), it->second.size() + 1);
        return it->second.size() + 1;
    }
    size_t putString(const char* key, const char* value) {
        strings[key] = value;
        return std::strlen(value);
    }
    size_t putUShort(const char*, uint16_t value) { return sizeof(value); }
    bool putUChar(const char* key, uint8_t value) {
        if (!write_ok) return false;
        if (std::strcmp(key, "audio_vol") == 0) {
            saved_volume = value;
            ++volume_writes;
        }
        return true;
    }
    size_t putUInt(const char* key, uint32_t value) {
        if (std::strcmp(key, "magic") == 0) magic = value;
        return sizeof(value);
    }
    size_t putChar(const char* key, int8_t value) {
        if (!mic_write_ok) return 0;
        if (std::strcmp(key, "mic_gain") == 0) { mic_gain = value; mic_gain_present = true; }
        return sizeof(value);
    }
    size_t putBool(const char* key, bool value) {
        if (std::strcmp(key, "mic_rumble") == 0) {
            if (!mic_write_ok) return 0;
            mic_rumble = value;
            mic_rumble_present = true;
        }
        return sizeof(uint8_t);
    }
};