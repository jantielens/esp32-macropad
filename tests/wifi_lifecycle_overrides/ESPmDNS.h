#pragma once
struct TestLifecycleMDNS {
    unsigned starts = 0, stops = 0;
    bool begin(const char*) { ++starts; return true; }
    void end() { ++stops; }
    void addService(const char*, const char*, unsigned) {}
    void addServiceTxt(const char*, const char*, const char*, const char*) {}
};
inline TestLifecycleMDNS MDNS;
