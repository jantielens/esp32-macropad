#pragma once

#include <Arduino.h>

class TwoWire {
public:
    uint8_t registers[128][256] = {};
    uint8_t address = 0;
    uint8_t bytes[2] = {};
    size_t count = 0;
    int response = -1;
    uint8_t responseRegister = 0;
    uint8_t panelVersion = 1;
    bool fail = false;

    bool begin(int sda, int scl, uint32_t frequency);
    void beginTransmission(uint8_t target);
    size_t write(uint8_t value);
    int endTransmission(bool stop = true);
    size_t requestFrom(uint8_t target, uint8_t length);
    int read();
};

extern TwoWire Wire;