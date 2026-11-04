#include "board_hardware.h"
#include "tab5_hardware.h"
#include "i2c_bus.h"
#include "log_manager.h"
#include <Arduino.h>
#include <Wire.h>
#include <esp32-hal-hosted.h>
#include <stdlib.h>

namespace {
constexpr uint8_t expanderAddress = 0x43;
constexpr uint8_t powerExpanderAddress = 0x44;
constexpr uint8_t powerMonitorAddress = 0x41;
constexpr uint8_t wifiPowerMask = 0x01;
constexpr uint8_t chargeControlMask = 0xa0;
constexpr uint8_t amplifierMask = 0x02;
constexpr uint8_t displayResetMask = 0x10;
constexpr uint8_t touchResetMask = 0x20;
uint8_t panelRevision = 0;
bool boardReady = false;
bool powerMonitorConfigured = false;

bool readPowerRegister(uint8_t reg, uint16_t& value) {
    Wire.beginTransmission(powerMonitorAddress);
    const bool written = Wire.write(reg) == 1;
    if (Wire.endTransmission(false) != 0 || !written ||
        Wire.requestFrom(powerMonitorAddress, uint8_t(2)) != 2) return false;
    const int high = Wire.read();
    const int low = Wire.read();
    if (high < 0 || low < 0) return false;
    value = uint16_t((uint16_t(high) << 8) | uint16_t(low));
    return true;
}

bool writePowerRegister(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(powerMonitorAddress);
    const bool written = Wire.write(reg) == 1 && Wire.write(uint8_t(value >> 8)) == 1 &&
        Wire.write(uint8_t(value)) == 1;
    return Wire.endTransmission() == 0 && written;
}

int readExpander(uint8_t reg, uint8_t address = expanderAddress) {
    Wire.beginTransmission(address);
    if (Wire.write(reg) != 1 || Wire.endTransmission(false) != 0 ||
        Wire.requestFrom(address, uint8_t(1)) != 1) return -1;
    return Wire.read();
}

bool updateExpander(uint8_t reg, uint8_t mask, uint8_t value, uint8_t address = expanderAddress) {
    const int current = readExpander(reg, address);
    if (current < 0) return false;
    Wire.beginTransmission(address);
    const bool written = Wire.write(reg) == 1 &&
        Wire.write(uint8_t((current & ~mask) | (value & mask))) == 1;
    return Wire.endTransmission() == 0 && written;
}

bool updateBootExpander(uint8_t reg, uint8_t mask, uint8_t value, uint8_t address = expanderAddress) {
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        if (updateExpander(reg, mask, value, address)) {
            if (attempt) LOGW("Tab5", "Expander recovered: address=0x%02x register=0x%02x attempt=%u",
                              address, reg, attempt + 1);
            return true;
        }
        if (attempt < 2) delay(10);
    }
    LOGE("Tab5", "Expander initialization failed: address=0x%02x register=0x%02x", address, reg);
    return false;
}

uint8_t detectPanel() {
    for (unsigned attempt = 0; attempt < 60; ++attempt) {
        Wire.beginTransmission(0x55);
        const bool written = Wire.write(uint8_t(0)) == 1 && Wire.write(uint8_t(0)) == 1;
        if (Wire.endTransmission(false) == 0 && written && Wire.requestFrom(uint8_t(0x55), uint8_t(1)) == 1) {
            const int version = Wire.read();
            if (version == 1 || version == 3) return static_cast<uint8_t>(version);
        }
        delay(10);
    }
    return 0;
}

bool setAmplifierMuted(bool muted) {
    if (!boardReady || !i2c_bus_lock()) return false;
    const bool ok = updateExpander(0x05, amplifierMask, muted ? 0 : amplifierMask);
    int output = -1, direction = -1, highImpedance = -1;
    if (ok && !muted) {
        output = readExpander(0x05);
        direction = readExpander(0x03);
        highImpedance = readExpander(0x07);
    }
    i2c_bus_unlock();
    if (ok && !muted) {
           LOGI("Tab5", "Amplifier wake: output=%d direction=%d high_z=%d",
               output, direction, highImpedance);
    }
    return ok;
}
}

uint8_t tab5_panel_revision() { return panelRevision; }
bool tab5_battery_read(float& voltage, float& current) {
    if (!boardReady || !i2c_bus_lock()) return false;
    uint16_t identity = 0, status = 0, rawVoltage = 0, rawCurrent = 0;
    if (!powerMonitorConfigured) {
        powerMonitorConfigured = readPowerRegister(0xff, identity) && identity == 0x2260 &&
            writePowerRegister(0x00, 0x0527) && writePowerRegister(0x05, 16777);
    }
    const bool ok = powerMonitorConfigured && readPowerRegister(0x06, status) &&
        (status & 0x0008) && !(status & 0x0004) && readPowerRegister(0x02, rawVoltage) &&
        readPowerRegister(0x04, rawCurrent);
    i2c_bus_unlock();
    if (!ok) return false;
    voltage = rawVoltage * 0.00125f;
    current = -static_cast<int16_t>(rawCurrent) * (2.0f / 32768.0f);
    return true;
}
AudioBoardControl tab5_audio_control() {
    AudioBoardControl control;
    control.mute = setAmplifierMuted;
    return control;
}

bool board_hardware_init() {
    powerMonitorConfigured = false;
    pinMode(22, OUTPUT);
    digitalWrite(22, LOW);
    if (!hostedSetPins(12, 13, 11, 10, 9, 8, 15)) {
        LOGE("Tab5", "Failed to configure hosted Wi-Fi pins");
        abort();
    }
    if (!i2c_bus_lock()) return false;
    bool ready = Wire.begin(31, 32, 100000);
    if (ready) {
        pinMode(23, OUTPUT);
        digitalWrite(23, HIGH);
        ready = updateBootExpander(0x05, wifiPowerMask, wifiPowerMask, powerExpanderAddress) &&
            updateBootExpander(0x07, wifiPowerMask, 0, powerExpanderAddress) &&
            updateBootExpander(0x03, wifiPowerMask, wifiPowerMask, powerExpanderAddress) &&
            updateBootExpander(0x05, chargeControlMask, chargeControlMask, powerExpanderAddress) &&
            updateBootExpander(0x07, chargeControlMask, 0, powerExpanderAddress) &&
            updateBootExpander(0x03, chargeControlMask, chargeControlMask, powerExpanderAddress) &&
            updateBootExpander(0x05, amplifierMask | displayResetMask | touchResetMask, 0) &&
            updateBootExpander(0x03, amplifierMask | displayResetMask | touchResetMask,
                           amplifierMask | displayResetMask | touchResetMask) &&
            updateBootExpander(0x07, amplifierMask | displayResetMask | touchResetMask, 0) &&
            updateBootExpander(0x0d, displayResetMask, displayResetMask) &&
            updateBootExpander(0x0b, displayResetMask, displayResetMask);
        if (ready) {
            delay(10);
            ready = updateBootExpander(0x03, displayResetMask, 0) &&
                updateBootExpander(0x05, touchResetMask, touchResetMask);
        }
        pinMode(23, INPUT);
        if (ready) {
            delay(100);
            panelRevision = detectPanel();
            if (panelRevision) LOGI("Tab5", "ST touch firmware version %u", panelRevision);
            else LOGW("Tab5", "Unsupported or unavailable display controller");
        }
    }
    i2c_bus_unlock();
    boardReady = ready;
    if (!ready) LOGE("Tab5", "Shared I2C or display reset initialization failed");
    return ready;
}