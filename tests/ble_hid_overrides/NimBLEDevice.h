#pragma once

#include "BLEDevice.h"

class NimBLEUUID {
public:
    explicit NimBLEUUID(uint16_t) {}
};

class NimBLEAdvertisementData {
public:
    std::vector<uint8_t> bytes;
    bool addData(const uint8_t* data, size_t length) {
        bytes.assign(data, data + length);
        return TestBle::data_succeeds;
    }
    bool setFlags(uint8_t) { return TestBle::data_succeeds; }
    bool setServiceData(NimBLEUUID, const uint8_t* data, size_t length) {
        bytes.assign(data, data + length);
        return TestBle::data_succeeds;
    }
    bool setName(const char*) { return TestBle::data_succeeds; }
};

class NimBLEAdvertising {
public:
    void enableScanResponse(bool) {}
    void setMinInterval(uint16_t interval) { TestBle::event_parameters.itvl_min = interval; }
    void setMaxInterval(uint16_t interval) { TestBle::event_parameters.itvl_max = interval; }
    bool setConnectableMode(uint8_t mode) { TestBle::event_parameters.conn_mode = mode; return true; }
    bool setDiscoverableMode(uint8_t mode) { TestBle::event_parameters.disc_mode = mode; return true; }
    bool setAdvertisementData(const NimBLEAdvertisementData& data) {
        TestBle::advertisement = data.bytes;
        return TestBle::data_succeeds;
    }
    bool setScanResponseData(const NimBLEAdvertisementData&) { return TestBle::data_succeeds; }
    bool stop() {
        if (!TestBle::stop_succeeds) return false;
        TestBle::radio_advertising = false;
        return true;
    }
    bool start() {
        ++TestBle::event_starts;
        if (TestBle::event_start_result) return false;
        TestBle::radio_advertising = true;
        return true;
    }
};

class NimBLEDevice {
public:
    inline static bool initialized = false;
    inline static bool init_succeeds = true;
    inline static NimBLEAdvertising advertising;
    static void init(const char*) { ++TestBle::host_inits; initialized = init_succeeds; }
    static bool isInitialized() { return initialized; }
    static NimBLEAdvertising* getAdvertising() { return &advertising; }
    static esp_power_level_t getPowerLevel(esp_ble_power_type_t type) {
        return esp_ble_tx_power_get(type);
    }
    static bool setPowerLevel(esp_power_level_t power, esp_ble_power_type_t type) {
        test_set_power(power, type);
        return TestBle::power_succeeds;
    }
};
