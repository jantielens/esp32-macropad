#pragma once
#include <cstddef>
#include <map>
#include <string>

inline std::map<std::string, bool> test_power_preferences;
inline bool test_power_write_fails = false;

class Preferences {
public:
    bool begin(const char*, bool) { return true; }
    void end() {}
    bool getBool(const char* key, bool fallback = false) {
        const auto entry = test_power_preferences.find(key);
        return entry == test_power_preferences.end() ? fallback : entry->second;
    }
    size_t putBool(const char* key, bool value) {
        if (test_power_write_fails) return 0;
        test_power_preferences[key] = value;
        return 1;
    }
};
