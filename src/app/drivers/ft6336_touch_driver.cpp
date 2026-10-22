#include "ft6336_touch_driver.h"

#include "../board_config.h"
#include "../i2c_bus.h"
#include "log_manager.h"
#include "ft6336_touch_zones.h"

#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr uint8_t kAddress = 0x38;
constexpr uint8_t kTouchRegister = 0x02;
}

void FT6336_TouchDriver::init() {
    if (!i2c_bus_lock()) return;
    Wire.beginTransmission(kAddress);
    available = Wire.endTransmission() == 0;
    i2c_bus_unlock();
    if (available) LOGI("FT6336", "Touch controller ready at 0x%02X", kAddress);
    else LOGW("FT6336", "Touch controller not responding at 0x%02X", kAddress);
}

bool FT6336_TouchDriver::isTouched() {
    const TouchSample sample = readSample();
    return sample.status != TouchReadStatus::Error && sample.pressed;
}

bool FT6336_TouchDriver::getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure) {
    if (pressure) *pressure = 0;
    if (!x || !y) return false;
    const TouchSample sample = readSample();
    if (sample.status == TouchReadStatus::Error || !sample.pressed) return false;
    *x = sample.horizontal;
    *y = sample.vertical;
    return true;
}

TouchSample FT6336_TouchDriver::readSample() {
    TouchSample sample;
    sample.status = TouchReadStatus::Error;
    if (!available || !i2c_bus_lock()) return sample;

    uint8_t data[5] = {};
    Wire.beginTransmission(kAddress);
    const bool written = Wire.write(kTouchRegister) == 1;
    bool ok = written && Wire.endTransmission(false) == 0 &&
        Wire.requestFrom(kAddress, static_cast<uint8_t>(sizeof(data))) == sizeof(data) &&
        Wire.available() >= static_cast<int>(sizeof(data));
    if (ok) {
        for (size_t index = 0; index < sizeof(data); ++index) {
            const int value = Wire.read();
            if (value < 0) {
                ok = false;
                break;
            }
            data[index] = static_cast<uint8_t>(value);
        }
    }
    i2c_bus_unlock();
    if (!ok) return sample;

    const uint8_t contacts = data[0] & 0x0f;
    if (contacts > 2 || ((data[1] >> 6) == 1 && contacts)) return sample;
    sample.status = TouchReadStatus::Fresh;
    sample.pressed = contacts != 0;
    if (!sample.pressed) return sample;

    uint16_t x = static_cast<uint16_t>(((data[1] & 0x0f) << 8) | data[2]);
    uint16_t y = static_cast<uint16_t>(((data[3] & 0x0f) << 8) | data[4]);
    sample.virtual_button = ft6336_virtual_button_at(x, y);
    if (sample.virtual_button >= 0) {
        sample.horizontal = x;
        sample.vertical = y;
        return sample;
    }

    if (calibrated && xmax > xmin && ymax > ymin) {
        x = x < xmin ? xmin : x > xmax ? xmax : x;
        y = y < ymin ? ymin : y > ymax ? ymax : y;
        x = static_cast<uint32_t>(x - xmin) * (DISPLAY_WIDTH - 1) / (xmax - xmin);
        y = static_cast<uint32_t>(y - ymin) * (DISPLAY_HEIGHT - 1) / (ymax - ymin);
    }
    sample.horizontal = x < DISPLAY_WIDTH ? x : DISPLAY_WIDTH - 1;
    sample.vertical = y < DISPLAY_HEIGHT ? y : DISPLAY_HEIGHT - 1;
    return sample;
}

void FT6336_TouchDriver::setCalibration(uint16_t x_min, uint16_t x_max,
                                        uint16_t y_min, uint16_t y_max) {
    if (x_max <= x_min || y_max <= y_min) return;
    xmin = x_min;
    xmax = x_max;
    ymin = y_min;
    ymax = y_max;
    calibrated = true;
}

void FT6336_TouchDriver::setRotation(uint8_t) {
    // The Core2 panel and FT6336 already report landscape coordinates.
}