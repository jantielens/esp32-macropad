#pragma once

constexpr int WIFI_OFF = 0;
struct TestPowerWiFi {
    void disconnect(bool) {}
    void mode(int) {}
};
inline TestPowerWiFi WiFi;
