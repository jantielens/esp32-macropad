#pragma once
#include "WiFi.h"
inline bool test_wifi_dns_start_ok = true;
inline unsigned test_wifi_dns_starts = 0, test_wifi_dns_stops = 0, test_wifi_dns_requests = 0;
struct DNSServer {
    bool start(unsigned, const char*, IPAddress) { ++test_wifi_dns_starts; return test_wifi_dns_start_ok; }
    void stop() { ++test_wifi_dns_stops; }
    void processNextRequest() { ++test_wifi_dns_requests; }
};
