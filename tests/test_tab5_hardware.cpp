#include <cassert>
#include <cstdint>
#include <cmath>
#include <Wire.h>
#include "board_hardware.h"
#include "tab5_hardware.h"
#include "i2c_bus.h"
#include "drivers/st7123_touch_driver.h"

TwoWire Wire;
static bool locked = false;
static bool hostedConfigured = false;
static unsigned resetWrites = 0;

bool i2c_bus_lock(TickType_t) {
    assert(!locked);
    locked = true;
    return true;
}

void i2c_bus_unlock() {
    assert(locked);
    locked = false;
}

void pinMode(int, int) {}
void digitalWrite(int, int) {}
void delay(unsigned long) {}

bool hostedSetPins(int clock, int command, int data0, int data1, int data2, int data3, int reset) {
    assert(clock == 12 && command == 13 && data0 == 11 && data1 == 10 && data2 == 9 && data3 == 8 && reset == 15);
    hostedConfigured = true;
    return true;
}

bool TwoWire::begin(int sda, int scl, uint32_t frequency) {
    assert(locked && hostedConfigured && sda == 31 && scl == 32 && frequency == 100000);
    return true;
}

void TwoWire::beginTransmission(uint8_t target) {
    assert(locked);
    address = target;
    count = 0;
}

size_t TwoWire::write(uint8_t value) {
    assert(count < sizeof(bytes));
    bytes[count++] = value;
    return 1;
}

int TwoWire::endTransmission(bool) {
    if (fail) return 1;
    if (transientFailures) {
        --transientFailures;
        return 1;
    }
    if (address == 0x41 && count == 3) {
        powerRegisters[bytes[0]] = uint16_t(bytes[1] << 8) | bytes[2];
    } else if (count == 2 && address != 0x55 && address != 0x41) {
        if (address == 0x44 && bytes[0] == 0x03 && (bytes[1] & 0xa0) == 0xa0) {
            assert((registers[address][0x05] & 0xa0) == 0xa0);
            assert((registers[address][0x07] & 0xa0) == 0);
        }
        if (address == 0x43 && bytes[0] == 0x05) {
            assert((bytes[1] & 0x10) == 0);
            ++resetWrites;
        }
        if (address == 0x43 && bytes[0] == 0x03 &&
            (registers[address][0x03] & 0x10) && !(bytes[1] & 0x10)) {
            assert((registers[address][0x05] & 0x10) == 0);
            assert((registers[address][0x0b] & 0x10) != 0);
            assert((registers[address][0x0d] & 0x10) != 0);
        }
        registers[address][bytes[0]] = bytes[1];
    }
    return 0;
}

size_t TwoWire::requestFrom(uint8_t target, uint8_t length) {
    assert(locked && target == address);
    if (target == 0x41) {
        assert(count == 1 && length == 2);
        responseRegister = bytes[0];
        responseIndex = 0;
    } else if (target == 0x55) {
        assert(count == 2 && bytes[0] == 0);
        responseRegister = bytes[1];
        response = responseRegister == 0 ? panelVersion : -1;
    } else {
        assert(length == 1);
        response = registers[target][bytes[0]];
    }
    return length;
}

int TwoWire::read() {
    if (address == 0x41) {
        const uint16_t value = powerRegisters[responseRegister];
        return responseIndex++ == 0 ? value >> 8 : value & 0xff;
    }
    if (address == 0x55 && response < 0) return registers[address][responseRegister++];
    return response;
}

int main() {
    Wire.registers[0x44][0x05] = 0x1c;
    Wire.registers[0x44][0x07] = 0xff;
    Wire.registers[0x44][0x03] = 0x1c;
    Wire.registers[0x43][0x05] = 0xff;
    Wire.registers[0x43][0x07] = 0xff;
    Wire.transientFailures = 1;
    assert(board_hardware_init());
    assert(!locked && tab5_panel_revision() == 1 && resetWrites >= 2);
    assert(Wire.registers[0x44][0x05] == 0xbd);
    assert(Wire.registers[0x44][0x07] == 0x5e);
    assert(Wire.registers[0x44][0x03] == 0xbd);
    float voltage = -1, current = -1;
    Wire.powerRegisters[0xff] = 0;
    assert(!tab5_battery_read(voltage, current));
    assert(Wire.powerRegisters[0x05] == 0);
    Wire.powerRegisters[0xff] = 0x2260;
    assert(!tab5_battery_read(voltage, current));
    assert(Wire.powerRegisters[0x00] == 0x0527 && Wire.powerRegisters[0x05] == 16777);
    Wire.powerRegisters[0x06] = 0x0008;
    Wire.powerRegisters[0x02] = 6400;
    Wire.powerRegisters[0x04] = uint16_t(-8192);
    assert(tab5_battery_read(voltage, current));
    assert(std::fabs(voltage - 8.0f) < 0.001f && std::fabs(current - 0.5f) < 0.001f);
    Wire.powerRegisters[0x04] = 4096;
    assert(tab5_battery_read(voltage, current) && std::fabs(current + 0.25f) < 0.001f);
    Wire.powerRegisters[0x06] = 0x000c;
    assert(!tab5_battery_read(voltage, current));
    Wire.fail = true;
    assert(!tab5_battery_read(voltage, current));
    Wire.fail = false;
    assert((Wire.registers[0x43][0x03] & 0x30) == 0x20);
    assert((Wire.registers[0x43][0x07] & 0x32) == 0);
    assert((Wire.registers[0x43][0x0b] & 0x10) != 0);
    assert((Wire.registers[0x43][0x0d] & 0x10) != 0);
    AudioBoardControl control = tab5_audio_control();
    assert(control.mute && control.mute(false));
    assert((Wire.registers[0x43][0x05] & 0x02) != 0);
    assert(control.mute(true));
    assert((Wire.registers[0x43][0x05] & 0x02) == 0);
    ST7123TouchDriver touch;
    touch.init();
    Wire.registers[0x55][0x10] = 0x08;
    Wire.registers[0x55][0x09] = 2;
    Wire.registers[0x55][0x14] = 0x80;
    Wire.registers[0x55][0x15] = 100;
    Wire.registers[0x55][0x16] = 0x01;
    Wire.registers[0x55][0x17] = 0x2c;
    auto snapshot = touch.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Fresh && snapshot.count == 1);
    assert(snapshot.contacts[0].horizontal == 100 && snapshot.contacts[0].vertical == 300);
        Wire.registers[0x55][0x09] = 10;
        Wire.registers[0x55][0x45] = 0x80;
        Wire.registers[0x55][0x46] = 120;
        Wire.registers[0x55][0x47] = 0x01;
        Wire.registers[0x55][0x48] = 0x40;
        snapshot = touch.readSnapshot();
        assert(snapshot.status == TouchReadStatus::Fresh && snapshot.count == 2);
        assert(snapshot.contacts[1].id == 7 && snapshot.contacts[1].horizontal == 120 &&
            snapshot.contacts[1].vertical == 320);
        Wire.registers[0x55][0x09] = 2;
    touch.setRotation(1);
    snapshot = touch.readSnapshot();
    assert(snapshot.contacts[0].horizontal == 300 && snapshot.contacts[0].vertical == 619);
    Wire.registers[0x55][0x10] = 0;
    snapshot = touch.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Fresh && snapshot.count == 0);
    Wire.fail = true;
    assert(touch.readSnapshot().status == TouchReadStatus::Error);
    Wire.fail = false;
    Wire.panelVersion = 3;
    assert(board_hardware_init() && tab5_panel_revision() == 3);
    Wire.panelVersion = 2;
    assert(board_hardware_init() && tab5_panel_revision() == 0);
    Wire.fail = true;
    assert(!board_hardware_init());
    assert(!control.mute(false));
    return 0;
}