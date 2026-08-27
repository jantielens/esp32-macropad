#pragma once

class HostString {
public:
    const char* c_str() const;
};

class HostIpAddress {
public:
    HostString toString() const;
};

class HostWiFi {
public:
    HostString macAddress() const;
    int status() const;
    HostString SSID() const;
    HostIpAddress localIP() const;
    const char* getHostname() const;
};

extern HostWiFi WiFi;

#define WL_CONNECTED 3