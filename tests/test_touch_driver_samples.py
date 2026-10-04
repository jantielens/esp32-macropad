#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
arduino = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#define BOARD_CONFIG_H
#define LOG_MANAGER_H
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240
#define DISPLAY_ROTATION 0
#define TOUCH_I2C_SCL 22
#define TOUCH_I2C_SDA 21
#define TOUCH_INT 3
#define IRAM_ATTR
#define FALLING 1
#define LOGI(...) ((void)0)
#define LOGW(...) ((void)0)
#define LOGE(...) ((void)0)
static void (*mock_irq)() = nullptr;
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int, void (*callback)(), int) { mock_irq = callback; }
inline void delayMicroseconds(unsigned) {}
inline int32_t constrain(int32_t value, int32_t low, int32_t high) {
    return std::max(low, std::min(value, high));
}
inline long map(long value, long low, long high, long target_low, long target_high) {
    return (value - low) * (target_high - target_low) / (high - low) + target_low;
}
'''
wire = r'''
#pragma once
#include <Arduino.h>
#include <vector>
#include <initializer_list>
class TwoWire {
public:
    std::vector<uint8_t> response;
    std::vector<uint8_t> written;
    size_t position = 0;
    size_t requested_size = 0;
    unsigned requests = 0;
    uint8_t result = 0;
    size_t write_limit = size_t(-1);
    bool read_failure = false;
    bool begin_ok = true;
    bool begin(int, int, unsigned) { return begin_ok; }
    void beginTransmission(uint8_t) { written.clear(); }
    size_t write(const uint8_t* bytes, size_t size) {
        written.insert(written.end(), bytes, bytes + size);
        return std::min(size, write_limit);
    }
    size_t write(uint8_t value) { written.push_back(value); return std::min(size_t(1), write_limit); }
    uint8_t endTransmission(bool = true) { return result; }
    size_t requestFrom(uint8_t, uint8_t) { ++requests; position = 0; return requested_size; }
    int available() { return response.size() - position; }
    int read() {
        return read_failure || position >= response.size() ? -1 : response[position++];
    }
    void packet(std::initializer_list<uint8_t> bytes) {
        response = bytes;
        requested_size = response.size();
        result = 0;
        write_limit = size_t(-1);
        read_failure = false;
    }
};
static TwoWire Wire;
'''
spi = r'''
#pragma once
class SPIClass {};
'''
xpt = r'''
#pragma once
#include <Arduino.h>
#include <SPI.h>
struct TS_Point { int16_t x, y, z; };
static TS_Point mock_point{1000, 1000, 500};
static bool mock_wake = true, mock_buffered = false, mock_begin_ok = true;
static unsigned point_reads = 0;
class XPT2046_Touchscreen {
public:
    XPT2046_Touchscreen(uint8_t, uint8_t) {}
    bool begin() { return mock_begin_ok; }
    void setRotation(uint8_t) {}
    bool tirqTouched() { return mock_wake; }
    bool bufferEmpty() { return mock_buffered; }
    TS_Point getPoint() { ++point_reads; return mock_point; }
};
'''
cases = {
    "fallback": r'''
#include "touch_driver.h"
class CheckedDriver : public TouchDriver {
public:
    TouchSample sample;
    unsigned reads = 0;
    void init() override {}
    bool isTouched() override { assert(false); return false; }
    bool getTouch(uint16_t*, uint16_t*, uint16_t*) override { assert(false); return false; }
    TouchSample readSample() override { ++reads; return sample; }
    void setCalibration(uint16_t, uint16_t, uint16_t, uint16_t) override {}
    void setRotation(uint8_t) override {}
};
int main() {
    CheckedDriver driver;
    assert(driver.contactCapacity() == 1);
    driver.sample.pressed = true;
    driver.sample.horizontal = 12;
    driver.sample.vertical = 34;
    TouchSnapshotFilter filter;
    auto snapshot = driver.readSnapshot();
    assert(driver.reads == 1 && snapshot.count == 1 && snapshot.contacts[0].id == 0);
    assert(snapshot.contacts[0].horizontal == 12 && snapshot.contacts[0].vertical == 34);
    assert(filter.update(snapshot, 0).count == 1);
    driver.sample.status = TouchReadStatus::Unchanged;
    snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Unchanged && filter.update(snapshot, 10).count == 1);
    driver.sample.status = TouchReadStatus::Error;
    assert(filter.update(driver.readSnapshot(), 20).count == 1);
    driver.sample.status = TouchReadStatus::Unchanged;
    assert(filter.update(driver.readSnapshot(), 119).count == 1);
    assert(filter.update(driver.readSnapshot(), 120).count == 0 && filter.canceled);
    driver.sample.status = TouchReadStatus::Fresh;
    assert(filter.update(driver.readSnapshot(), 130).count == 0);
    driver.sample.status = TouchReadStatus::Unchanged;
    driver.sample.pressed = false;
    assert(filter.update(driver.readSnapshot(), 140).count == 0);
    driver.sample.status = TouchReadStatus::Fresh;
    assert(filter.update(driver.readSnapshot(), 150).count == 0);
    driver.sample.pressed = true;
    assert(filter.update(driver.readSnapshot(), 160).count == 1);
}
''',
    "axs": r'''
#include "drivers/axs15231b/vendor/AXS15231B_touch.cpp"
#include "drivers/axs15231b_touch_driver.cpp"
int main() {
    AXS15231B_TouchDriver driver;
    assert(driver.readSample().status == TouchReadStatus::Error);
    driver.init();
    driver.setCalibration(0, 319, 0, 239);
    assert(mock_irq);
    assert(driver.readSample().status == TouchReadStatus::Unchanged);
    Wire.packet({0, 1, 0, 100, 0, 80, 0, 0});
    mock_irq();
    TouchSampleFilter filter;
    auto sample = driver.readSample();
    assert(sample.status == TouchReadStatus::Fresh && sample.pressed);
    assert(sample.horizontal == 100 && sample.vertical == 80);
    assert(filter.update(sample, 0).pressed);
    const unsigned requests = Wire.requests;
    sample = driver.readSample();
    assert(sample.status == TouchReadStatus::Fresh && sample.pressed);
    assert(Wire.requests == requests + 1 && filter.update(sample, 500).pressed);
    Wire.packet({0, 1, 0xc0, 100, 0, 80, 0, 0});
    mock_irq();
    assert(driver.readSample().status == TouchReadStatus::Unchanged);
    Wire.result = 4;
    assert(filter.update(driver.readSample(), 600).pressed);
    assert(!filter.update(driver.readSample(), 700).pressed && filter.canceled);
    assert(Wire.requests == requests + 2);
    Wire.packet({0, 1, 0x80, 150, 0, 90, 0, 0});
    assert(!filter.update(driver.readSample(), 710).pressed);
    Wire.packet({0, 0, 0, 0, 0, 0, 0, 0});
    mock_irq();
    sample = driver.readSample();
    assert(sample.status == TouchReadStatus::Fresh && !sample.pressed);
    filter.update(sample, 720);
    Wire.packet({0, 1, 0x80, 150, 0, 90, 0, 0});
    mock_irq();
    assert(!driver.readSample().pressed);
    Wire.packet({0, 1, 0, 100, 0, 80, 0, 0});
    mock_irq();
    assert(filter.update(driver.readSample(), 730).pressed);
    Wire.write_limit = 5;
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.packet({0, 1, 0, 200, 0});
    Wire.requested_size = 8;
    mock_irq();
    sample = driver.readSample();
    assert(sample.status == TouchReadStatus::Error && sample.horizontal == 100);
    Wire.packet({0, 2, 0, 100, 0, 80, 0, 0});
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.packet({0, 1, 0, 100, 0, 80, 0, 0});
    Wire.read_failure = true;
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.packet({0, 1, 0, 100, 0, 80, 0, 0});
    driver.setRotation(1);
    sample = driver.readSample();
    assert(sample.status == TouchReadStatus::Fresh && sample.horizontal == 159 && sample.vertical == 100);
}
''',
    "cst": r'''
#include "drivers/wire_cst816s_touch_driver.cpp"
int main() {
    Wire_CST816S_TouchDriver driver;
    assert(driver.readSample().status == TouchReadStatus::Error);
    driver.init();
    Wire.packet({1, 0, 100, 0, 80});
    TouchSampleFilter filter;
    auto sample = driver.readSample();
    assert(sample.status == TouchReadStatus::Fresh && sample.pressed);
    assert(sample.horizontal == 100 && sample.vertical == 80);
    assert(filter.update(sample, 0).pressed);
    assert(filter.update(driver.readSample(), 500).pressed);
    Wire.result = 4;
    assert(driver.readSample().status == TouchReadStatus::Error);
    assert(filter.update(driver.readSample(), 600).pressed);
    assert(!filter.update(driver.readSample(), 700).pressed && filter.canceled);
    Wire.packet({1, 0, 100, 0, 80});
    assert(!filter.update(driver.readSample(), 710).pressed);
    Wire.packet({1, 0x40, 100, 0, 80});
    sample = driver.readSample();
    assert(sample.status == TouchReadStatus::Fresh && !sample.pressed);
    filter.update(sample, 720);
    Wire.packet({1, 0, 100, 0, 80});
    assert(filter.update(driver.readSample(), 730).pressed);
    Wire.packet({0, 0, 0, 0, 0});
    assert(!driver.readSample().pressed);
    Wire.packet({1, 0, 100});
    Wire.requested_size = 5;
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.packet({2, 0, 100, 0, 80});
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.packet({1, 0xc0, 100, 0, 80});
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.packet({1, 0, 100, 0, 80});
    Wire.read_failure = true;
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.packet({1, 0, 100, 0, 80});
    Wire.write_limit = 0;
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.packet({1, 0, 100, 0, 80});
    Wire.requested_size = 4;
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.packet({1, 0, 100, 0, 80});
    driver.setCalibration(0, 319, 0, 239);
    driver.setRotation(1);
    sample = driver.readSample();
    assert(sample.horizontal == 80 && sample.vertical == 219);
    uint16_t horizontal = 0, vertical = 0, pressure = 99;
    assert(driver.getTouch(&horizontal, &vertical, &pressure));
    assert(horizontal == 80 && vertical == 219 && pressure == 0);
    Wire.result = 1;
    driver.init();
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.result = 0;
    driver.init();
    assert(driver.readSample().status == TouchReadStatus::Fresh);
    Wire.write_limit = 0;
    driver.init();
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.begin_ok = false;
    driver.init();
    assert(driver.readSample().status == TouchReadStatus::Error);
}
''',
    "xpt": r'''
#include "drivers/xpt2046_driver.cpp"
int main() {
    XPT2046_Driver driver(5, 3);
    assert(driver.readSample().status == TouchReadStatus::Error);
    driver.init();
    driver.setCalibration(0, 319, 0, 239);
    mock_point = {100, 80, 500};
    TouchSampleFilter filter;
    auto sample = driver.readSample();
    assert(sample.status == TouchReadStatus::Fresh && sample.pressed);
    assert(sample.horizontal == 100 && sample.vertical == 80);
    assert(filter.update(sample, 0).pressed);
    mock_buffered = true;
    const unsigned reads = point_reads;
    sample = driver.readSample();
    assert(sample.status == TouchReadStatus::Unchanged && sample.pressed && point_reads == reads);
    assert(filter.update(sample, 500).pressed);
    mock_buffered = false;
    mock_point = {8191, 80, 500};
    assert(filter.update(driver.readSample(), 600).pressed);
    assert(!filter.update(driver.readSample(), 700).pressed && filter.canceled);
    mock_point = {100, 80, 500};
    assert(!filter.update(driver.readSample(), 710).pressed);
    mock_point.z = 0;
    filter.update(driver.readSample(), 720);
    mock_point.z = 500;
    assert(filter.update(driver.readSample(), 730).pressed);
    mock_wake = false;
    assert(!driver.readSample().pressed);
    mock_wake = true;
    mock_point = {-1, 80, 500};
    assert(driver.readSample().status == TouchReadStatus::Error);
    mock_point = {100, 80, 4095};
    assert(driver.readSample().status == TouchReadStatus::Error);
    mock_point = {100, 80, 500};
    uint16_t horizontal = 0, vertical = 0, pressure = 0;
    assert(driver.getTouch(&horizontal, &vertical, &pressure));
    assert(horizontal == 100 && vertical == 80 && pressure == 500);
    mock_begin_ok = false;
    driver.init();
    assert(driver.readSample().status == TouchReadStatus::Error);
}
''',
}

with tempfile.TemporaryDirectory() as directory:
    temporary = pathlib.Path(directory)
    (temporary / "Arduino.h").write_text(arduino)
    (temporary / "Wire.h").write_text(wire)
    (temporary / "SPI.h").write_text(spi)
    (temporary / "XPT2046_Touchscreen.h").write_text(xpt)
    for name, case in cases.items():
        source = temporary / f"{name}.cpp"
        executable = temporary / name
        source.write_text(case)
        subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                        "-I", str(temporary), "-I", str(root / "src/app"),
                        "-include", str(temporary / "Arduino.h"),
                        str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        print(f"PASS: {name} checked touch samples")