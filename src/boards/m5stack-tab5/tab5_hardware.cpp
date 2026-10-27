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
constexpr uint8_t wifiPowerMask = 0x01;
constexpr uint8_t amplifierMask = 0x02;
constexpr uint8_t displayResetMask = 0x10;
constexpr uint8_t touchResetMask = 0x20;
uint8_t panelRevision = 0;
bool boardReady = false;

bool updateExpander(uint8_t reg, uint8_t mask, uint8_t value, uint8_t address = expanderAddress) {
    Wire.beginTransmission(address);
    if (Wire.write(reg) != 1 || Wire.endTransmission(false) != 0 ||
        Wire.requestFrom(address, uint8_t(1)) != 1) return false;
    const int current = Wire.read();
    if (current < 0) return false;
    Wire.beginTransmission(address);
    const bool written = Wire.write(reg) == 1 &&
        Wire.write(uint8_t((current & ~mask) | (value & mask))) == 1;
    return Wire.endTransmission() == 0 && written;
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
    i2c_bus_unlock();
    return ok;
}
}

uint8_t tab5_panel_revision() { return panelRevision; }
AudioBoardControl tab5_audio_control() {
    AudioBoardControl control;
    control.mute = setAmplifierMuted;
    return control;
}

bool board_hardware_init() {
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
        ready = updateExpander(0x05, wifiPowerMask, wifiPowerMask, powerExpanderAddress) &&
            updateExpander(0x07, wifiPowerMask, 0, powerExpanderAddress) &&
            updateExpander(0x03, wifiPowerMask, wifiPowerMask, powerExpanderAddress) &&
            updateExpander(0x05, amplifierMask | displayResetMask | touchResetMask, 0) &&
            updateExpander(0x03, amplifierMask | displayResetMask | touchResetMask,
                           amplifierMask | displayResetMask | touchResetMask) &&
            updateExpander(0x07, amplifierMask | displayResetMask | touchResetMask, 0) &&
            updateExpander(0x0d, displayResetMask, displayResetMask) &&
            updateExpander(0x0b, displayResetMask, displayResetMask);
        if (ready) {
            delay(10);
            ready = updateExpander(0x03, displayResetMask, 0) &&
                updateExpander(0x05, touchResetMask, touchResetMask);
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