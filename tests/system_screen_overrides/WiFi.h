#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

constexpr int WL_CONNECTED = 3;
constexpr int WIFI_OFF = 0;
constexpr int WIFI_STA = 1;

struct IPAddress {
    uint8_t bytes[4];
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : bytes{a, b, c, d} {}
    bool operator!=(const IPAddress& other) const {
        for (int i = 0; i < 4; ++i) {
            if (bytes[i] != other.bytes[i]) return true;
        }
        return false;
    }
    std::string toString() const {
        char text[16];
        snprintf(text, sizeof(text), "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
        return text;
    }
};

struct SystemScreenWiFi {
    bool connected = true;
    int mode = WIFI_STA;
    std::string ssid = "Home Wi-Fi";
    std::string hostname = "desk-device";
    IPAddress ip{192, 168, 1, 42};
    int status() const { return connected ? WL_CONNECTED : 0; }
    int getMode() const { return mode; }
    std::string SSID() const { return ssid; }
    IPAddress localIP() const { return ip; }
    const char* getHostname() const { return hostname.c_str(); }
};

inline SystemScreenWiFi WiFi;
