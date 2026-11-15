#pragma once
#include "Arduino.h"
#include <array>
#include <functional>
#include <vector>

enum wl_status_t { WL_IDLE_STATUS, WL_NO_SSID_AVAIL, WL_SCAN_COMPLETED,
    WL_CONNECTED, WL_CONNECT_FAILED, WL_CONNECTION_LOST, WL_DISCONNECTED };
enum wifi_mode_t { WIFI_OFF, WIFI_STA, WIFI_AP };
enum arduino_event_id_t { ARDUINO_EVENT_WIFI_STA_DISCONNECTED, ARDUINO_EVENT_WIFI_STA_GOT_IP };
struct arduino_event_info_t { struct { uint8_t reason = 0; } wifi_sta_disconnected; };

class IPAddress {
    uint8_t bytes_[4] = {};
public:
    IPAddress() = default;
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : bytes_{a,b,c,d} {}
    uint8_t operator[](size_t i) const { return bytes_[i]; }
    bool operator==(const IPAddress& other) const { return !std::memcmp(bytes_, other.bytes_, 4); }
    bool operator!=(const IPAddress& other) const { return !(*this == other); }
    bool fromString(const char* text) {
        unsigned a,b,c,d;
        if (std::sscanf(text, "%u.%u.%u.%u", &a,&b,&c,&d) != 4 ||
                a > 255 || b > 255 || c > 255 || d > 255) return false;
        *this = IPAddress(a,b,c,d);
        return true;
    }
    String toString() const {
        char text[16];
        std::snprintf(text, sizeof(text), "%u.%u.%u.%u", bytes_[0],bytes_[1],bytes_[2],bytes_[3]);
        return text;
    }
};
struct esp_netif_t {};
inline esp_netif_t* esp_netif_get_handle_from_ifkey(const char*) { return nullptr; }
inline int esp_netif_set_hostname(esp_netif_t*, const char*) { return 0; }

struct TestLifecycleWiFi {
    struct AccessPoint {
        std::string ssid;
        int rssi;
        int channel;
        std::array<uint8_t, 6> bssid;
    };
    wifi_mode_t radio_mode = WIFI_OFF;
    wl_status_t association = WL_DISCONNECTED;
    wl_status_t begin_result = WL_IDLE_STATUS;
    bool auto_reconnect = false, mode_ok = true, ap_config_ok = true, ap_ok = true;
    bool static_config_ok = true;
    unsigned begins = 0, disconnects = 0, ap_disconnects = 0, modes = 0;
    unsigned scans = 0;
    bool scan_ok = true, scan_results_live = false;
    unsigned long scan_duration_ms = 0;
    std::vector<AccessPoint> access_points;
    int begin_channel = 0;
    std::array<uint8_t, 6> begin_bssid = {};
    mutable unsigned diagnostic_reads = 0;
    mutable unsigned ssid_reads = 0;
    IPAddress station_ip = IPAddress(192,168,1,42);
    int rssi = -52, channel_number = 6;
    uint8_t bssid[6] = {1,2,3,4,5,6};
    std::string hostname;
    std::vector<wifi_mode_t> mode_history;
    std::function<void(arduino_event_id_t, arduino_event_info_t)> disconnected, got_ip;
    void persistent(bool) {}
    void setSleep(bool) {}
    void setAutoReconnect(bool enabled) { auto_reconnect = enabled; }
    bool mode(wifi_mode_t value) {
        ++modes;
        mode_history.push_back(value);
        if (!mode_ok) return false;
        radio_mode = value;
        return true;
    }
    wifi_mode_t getMode() const { return radio_mode; }
    wl_status_t status() const { return association; }
    bool disconnect(bool) { ++disconnects; association = WL_DISCONNECTED; return true; }
    wl_status_t begin(const char*, const char*, int channel = 0, const uint8_t* selected_bssid = nullptr) {
        begin_channel = channel;
        begin_bssid = {};
        if (selected_bssid) std::memcpy(begin_bssid.data(), selected_bssid, begin_bssid.size());
        ++begins; association = begin_result; return begin_result;
    }
    void setHostname(const char* value) { hostname = value; }
    const char* getHostname() const { return hostname.c_str(); }
    bool config(IPAddress, IPAddress, IPAddress, IPAddress, IPAddress) { return static_config_ok; }
    void scanDelete() { scan_results_live = false; }
    int16_t scanNetworks() {
        ++scans;
        test_wifi_now += scan_duration_ms;
        scan_results_live = scan_ok;
        return scan_ok ? static_cast<int16_t>(access_points.size()) : -1;
    }
    String SSID(int index) const { return access_points.at(index).ssid.c_str(); }
    String SSID() const { ++ssid_reads; return "saved-network"; }
    int RSSI(int index = -1) const {
        ++diagnostic_reads;
        return index < 0 ? rssi : access_points.at(index).rssi;
    }
    const uint8_t* BSSID(int index = -1) const {
        ++diagnostic_reads;
        return index < 0 ? bssid : access_points.at(index).bssid.data();
    }
    int channel(int index = -1) const {
        ++diagnostic_reads;
        return index < 0 ? channel_number : access_points.at(index).channel;
    }
    IPAddress localIP() const { return station_ip; }
    IPAddress gatewayIP() const { return IPAddress(); }
    String macAddress() const { return "11:22:33:44:55:66"; }
    void onEvent(std::function<void(arduino_event_id_t, arduino_event_info_t)> callback,
                 arduino_event_id_t event) {
        (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED ? disconnected : got_ip) = callback;
    }
    void emit(arduino_event_id_t event, uint8_t reason = 0) {
        arduino_event_info_t info;
        info.wifi_sta_disconnected.reason = reason;
        auto& callback = event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED ? disconnected : got_ip;
        if (callback) callback(event, info);
    }
    bool softAPConfig(IPAddress, IPAddress, IPAddress) { return ap_config_ok; }
    bool softAP(const char*, const char*, int = 1, int = 0, int = 4) { return ap_ok; }
    bool softAPdisconnect(bool) { ++ap_disconnects; return true; }
    void softAPsetHostname(const char*) {}
    IPAddress softAPIP() const { return IPAddress(192,168,4,1); }
};
inline TestLifecycleWiFi WiFi;
