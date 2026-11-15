#pragma once
#include "../Arduino.h"
#include <cstdio>
#include <string>

class String : public std::string {
public:
    using std::string::string;
    String(const std::string& text) : std::string(text) {}
    void replace(const char* from, const char* to) {
        size_t pos = 0;
        while ((pos = find(from, pos)) != npos) {
            std::string::replace(pos, std::strlen(from), to);
            pos += std::strlen(to);
        }
    }
    String substring(size_t start) const { return substr(start); }
};

inline unsigned long test_wifi_now = 1000;
inline unsigned long test_wifi_delay_ms = 0;
inline void delay(unsigned long duration) {
    test_wifi_now += duration;
    test_wifi_delay_ms += duration;
}
struct TestWifiESP {
    uint64_t getEfuseMac() const { return 0x112233445566ULL; }
    const char* getChipModel() const { return "host"; }
};
inline TestWifiESP ESP;
#define RTC_DATA_ATTR
