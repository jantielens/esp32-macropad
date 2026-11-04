#include "st7123_touch_driver.h"
#include "../board_config.h"
#include "../i2c_bus.h"
#include BOARD_HARDWARE_HEADER
#include <Wire.h>

namespace {
constexpr uint8_t maxTouchRecords = 10;

bool readTouchRegister(uint16_t reg, uint8_t* data, uint8_t length) {
    Wire.beginTransmission(0x55);
    const bool written = Wire.write(uint8_t(reg >> 8)) == 1 && Wire.write(uint8_t(reg)) == 1;
    if (Wire.endTransmission(false) != 0 || !written || Wire.requestFrom(uint8_t(0x55), length) != length) return false;
    for (uint8_t index = 0; index < length; ++index) {
        const int value = Wire.read();
        if (value < 0) return false;
        data[index] = uint8_t(value);
    }
    return true;
}
}

void ST7123TouchDriver::init() { available = tab5_panel_revision() != 0; }

bool ST7123TouchDriver::isTouched() {
    const TouchSnapshot snapshot = readSnapshot();
    return snapshot.status != TouchReadStatus::Error && snapshot.count != 0;
}

bool ST7123TouchDriver::getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure) {
    if (pressure) *pressure = 0;
    if (!x || !y) return false;
    const TouchSample sample = readSample();
    if (sample.status == TouchReadStatus::Error || !sample.pressed) return false;
    *x = sample.horizontal;
    *y = sample.vertical;
    return true;
}

TouchSample ST7123TouchDriver::readSample() {
    const TouchSnapshot snapshot = readSnapshot();
    TouchSample sample;
    sample.status = snapshot.status;
    sample.pressed = snapshot.count != 0;
    if (sample.pressed) {
        sample.horizontal = snapshot.contacts[0].horizontal;
        sample.vertical = snapshot.contacts[0].vertical;
    }
    return sample;
}

TouchSnapshot ST7123TouchDriver::readSnapshot() {
    TouchSnapshot result;
    result.status = TouchReadStatus::Error;
    if (!available || !i2c_bus_lock()) return result;
    uint8_t info = 0, count = 0;
    uint8_t records[7 * maxTouchRecords] = {};
    bool ok = readTouchRegister(0x0010, &info, 1);
    if (ok && (info & 0x08)) {
        ok = readTouchRegister(0x0009, &count, 1);
        if (count > maxTouchRecords) count = maxTouchRecords;
        if (ok && count) ok = readTouchRegister(0x0014, records, uint8_t(count * 7));
    }
    i2c_bus_unlock();
    if (!ok) return result;
    result.status = TouchReadStatus::Fresh;
    for (uint8_t index = 0; index < count; ++index) {
        const uint8_t* record = records + index * 7;
        if (!(record[0] & 0x80)) continue;
        uint16_t x = uint16_t((record[0] & 0x3f) << 8) | record[1];
        uint16_t y = uint16_t(record[2] << 8) | record[3];
        if (x < xmin) x = xmin;
        if (x > xmax) x = xmax;
        if (y < ymin) y = ymin;
        if (y > ymax) y = ymax;
        x = uint32_t(x - xmin) * (DISPLAY_WIDTH - 1) / (xmax - xmin);
        y = uint32_t(y - ymin) * (DISPLAY_HEIGHT - 1) / (ymax - ymin);
        uint16_t tx = x, ty = y;
        if (rotation == 1) { tx = y; ty = DISPLAY_WIDTH - 1 - x; }
        if (rotation == 2) { tx = DISPLAY_WIDTH - 1 - x; ty = DISPLAY_HEIGHT - 1 - y; }
        if (rotation == 3) { tx = DISPLAY_HEIGHT - 1 - y; ty = x; }
        result.contacts[result.count].id = index;
        result.contacts[result.count].horizontal = tx;
        result.contacts[result.count].vertical = ty;
        ++result.count;
        if (result.count == TOUCH_CONTACT_CAPACITY) break;
    }
    return result;
}

void ST7123TouchDriver::setCalibration(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1) {
    if (x1 <= x0 || y1 <= y0) return;
    xmin = x0;
    xmax = x1;
    ymin = y0;
    ymax = y1;
}