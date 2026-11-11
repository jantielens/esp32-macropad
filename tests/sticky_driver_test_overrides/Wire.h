#pragma once
#include <Arduino.h>
#include <array>

inline uint8_t highByte(uint16_t value) { return value >> 8; }
inline uint8_t lowByte(uint16_t value) { return value & 255; }

class TwoWire {
public:
    explicit TwoWire(unsigned number) : bus(number) {}
    unsigned bus, begins = 0;
    int sda = -1, scl = -1;
    uint32_t frequency = 0;
    bool failBegin = false, failRead = false, failWrite = false;
    std::array<uint8_t, 65536> registers{};
    uint16_t reg = 0;
    unsigned writes = 0;
    bool begin(int dataPin, int clockPin, uint32_t hz) {
        ++begins; sda = dataPin; scl = clockPin; frequency = hz;
        return !failBegin;
    }
    void beginTransmission(uint8_t) { writes = 0; }
    size_t write(uint8_t value) {
        if (writes == 0) reg = uint16_t(value) << 8;
        else if (writes == 1) reg |= value;
        else registers[reg++] = value;
        ++writes;
        return 1;
    }
    uint8_t endTransmission() { return failWrite ? 4 : 0; }
    size_t requestFrom(uint8_t, uint8_t length) { return failRead ? 0 : length; }
    int read() { return registers[reg++]; }
};
extern TwoWire Wire, Wire1;
