#pragma once
#include <Wire.h>

extern int shtTestError;
extern float shtTestTemperature, shtTestHumidity;
extern unsigned shtTestSamples;
extern bool shtTestLockHeld;
bool sticky_test_bus_locked();

class SensirionI2cSht4x {
public:
    void begin(TwoWire &wire, uint8_t address) {
        shtTestLockHeld = (&wire == &Wire && address == 0x44);
    }
    int16_t measureHighPrecision(float &temperature, float &humidity) {
        shtTestLockHeld = shtTestLockHeld && sticky_test_bus_locked();
        ++shtTestSamples;
        temperature = shtTestTemperature;
        humidity = shtTestHumidity;
        return shtTestError;
    }
};
