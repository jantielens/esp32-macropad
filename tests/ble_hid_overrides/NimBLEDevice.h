#pragma once

#include "BLEDevice.h"

class NimBLEAdvertisementData {
public:
    std::vector<uint8_t> bytes;
    bool addData(const uint8_t* data, size_t length) {
        bytes.assign(data, data + length);
        return true;
    }
};

class NimBLEAdvertising : public BLEAdvertising {
public:
    void enableScanResponse(bool) {}
    void setMinInterval(uint16_t interval) { TestBle::event_parameters.itvl_min = interval; }
    void setMaxInterval(uint16_t interval) { TestBle::event_parameters.itvl_max = interval; }
    bool setConnectableMode(uint8_t mode) { TestBle::event_parameters.conn_mode = mode; return true; }
    bool setDiscoverableMode(uint8_t mode) { TestBle::event_parameters.disc_mode = mode; return true; }
    bool setAdvertisementData(const NimBLEAdvertisementData& data) {
        TestBle::advertisement = data.bytes;
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
    inline static NimBLEAdvertising advertising;
    static void init(const char* name) { BLEDevice::init(name); }
    static bool isInitialized() { return BLEDevice::getInitialized(); }
    static NimBLEAdvertising* getAdvertising() { return &advertising; }
};
