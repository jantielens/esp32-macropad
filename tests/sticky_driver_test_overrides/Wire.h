#pragma once
#include <Arduino.h>
#include <array>

bool sticky_test_bus_locked();
extern bool sticky_test_unlocked_wire0;
extern bool sticky_test_invalid_gauge_transaction;

inline uint8_t highByte(uint16_t value) { return value >> 8; }
inline uint8_t lowByte(uint16_t value) { return value & 255; }

class TwoWire {
public:
    explicit TwoWire(unsigned number) : bus(number) {}
    unsigned bus, begins = 0;
    int sda = -1, scl = -1;
    uint32_t frequency = 0;
    bool failBegin = false, failRead = false, failWrite = false;
    int nackRegister = -1, shortRegister = -1, failedByteRegister = -1;
    bool failByteWrite = false;
    uint8_t address = 0;
    unsigned transactions = 0;
    std::array<uint8_t, 65536> registers{};
    uint16_t reg = 0;
    uint16_t startReg = 0;
    unsigned writes = 0;
    bool begin(int dataPin, int clockPin, uint32_t hz) {
        ++begins; sda = dataPin; scl = clockPin; frequency = hz;
        return !failBegin;
    }
    void beginTransmission(uint8_t target) {
        if (bus == 0 && !sticky_test_bus_locked()) sticky_test_unlocked_wire0 = true;
        address = target; writes = 0; ++transactions;
    }
    size_t write(uint8_t value) {
        if (bus == 0 && failByteWrite) return 0;
        if (bus == 0 && address == 0x51) {
            if (writes == 0) startReg = reg = value;
            else {
                if (reg == failedByteRegister) return 0;
                registers[reg++] = value;
            }
            ++writes;
            return 1;
        }
        if (bus == 0) {
            if (address != 0x55 || writes != 0 ||
                (value != 0x08 && value != 0x2c && value != 0x0a && value != 0x0c &&
                 value != 0x16 && value != 0x18))
                sticky_test_invalid_gauge_transaction = true;
            reg = value; ++writes; return 1;
        }
        if (writes == 0) reg = uint16_t(value) << 8;
        else if (writes == 1) reg |= value;
        else registers[reg++] = value;
        ++writes;
        return 1;
    }
    uint8_t endTransmission(bool stop = true) {
        if (bus == 0 && address == 0x51)
            return failWrite || startReg == nackRegister ? 4 : 0;
        if (bus == 0 && writes == 1 && stop) sticky_test_invalid_gauge_transaction = true;
        return failWrite || (bus == 0 && reg == nackRegister) ? 4 : 0;
    }
    size_t requestFrom(uint8_t target, uint8_t length) {
        if (bus == 0 && target == 0x51)
            return failRead ? 0 : (reg == shortRegister ? length - 1 : length);
        if (bus == 0 && (target != 0x55 || length != 2)) sticky_test_invalid_gauge_transaction = true;
        return failRead ? 0 : (bus == 0 && reg == shortRegister ? 1 : length);
    }
    int read() {
        if (bus == 0 && reg == failedByteRegister) return -1;
        return registers[reg++];
    }
};
extern TwoWire Wire, Wire1;
