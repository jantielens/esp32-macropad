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
#include <cstdarg>
#include <cstdio>
#include <string>
#define BOARD_CONFIG_H
#define LOG_MANAGER_H
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240
#define DISPLAY_ROTATION 0
#define TOUCH_I2C_SCL 22
#define TOUCH_I2C_SDA 21
#define TOUCH_INT 3
#ifndef MAX_AXS15231B_CONTACTS
#define MAX_AXS15231B_CONTACTS 1
#endif
#define IRAM_ATTR
#define FALLING 1
#define LOGI(...) ((void)0)
static unsigned invalid_report_log_count = 0;
inline void mock_trace(const char*, const char* format, ...) {
    if (std::string(format).find("Invalid active report:") == 0) ++invalid_report_log_count;
}
#define LOGT(...) mock_trace(__VA_ARGS__)
static unsigned warning_count = 0;
static std::string last_warning;
static uint32_t mock_now = 0;
inline uint32_t millis() { return mock_now; }
inline void mock_warning(const char*, const char* format, ...) {
    char message[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    last_warning = message;
    ++warning_count;
}
#define LOGW(...) mock_warning(__VA_ARGS__)
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
    size_t requested_length = 0;
    size_t packet_padding = 0;
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
    size_t requestFrom(uint8_t, uint8_t length) { ++requests; position = 0; requested_length = length; return requested_size; }
    int available() { return response.size() - position; }
    int read() {
        return read_failure || position >= response.size() ? -1 : response[position++];
    }
    void packet(std::initializer_list<uint8_t> bytes) {
        response = bytes;
        if (response.size() == 8 && packet_padding) response.resize(packet_padding, 0);
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
    assert(Wire.requested_length == 8 && Wire.written.size() == 11);
    assert(Wire.written[6] == 0 && Wire.written[7] == 8);
    assert(sample.status == TouchReadStatus::Fresh && sample.pressed);
    assert(sample.horizontal == 100 && sample.vertical == 80);
    assert(filter.update(sample, 0).pressed);
    const unsigned requests = Wire.requests;
    sample = driver.readSample();
    assert(sample.status == TouchReadStatus::Fresh && sample.pressed);
    assert(Wire.requests == requests + 1 && filter.update(sample, 500).pressed);
    Wire.packet({0, 1, 0xc0, 100, 0, 80, 0, 0});
    mock_irq();
    sample = driver.readSample();
    assert(sample.status == TouchReadStatus::Fresh && !sample.pressed);
    Wire.result = 4;
    mock_irq();
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
    sample = driver.readSample();
    assert(sample.status == TouchReadStatus::Error && sample.pressed && sample.horizontal == 100);
    Wire.packet({0, 1, 0, 100, 0, 80, 0, 0});
    Wire.read_failure = true;
    mock_irq();
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.packet({0, 1, 0, 100, 0, 80, 0, 0});
    driver.setRotation(1);
    sample = driver.readSample();
    assert(sample.status == TouchReadStatus::Fresh && sample.horizontal == 159 && sample.vertical == 100);

    AXS15231B_Touch polling(22, 21, 0xff, 0x3b, 0);
    assert(polling.begin());
    polling.setOffsets(0, 319, 319, 0, 239, 239);
    TouchSnapshotFilter snapshot_filter;
    auto read_snapshot = [&]() {
        TouchSnapshot snapshot;
        const auto status = polling.readSample();
        snapshot.status = status == AXS15231B_Touch::ReadStatus::Fresh ? TouchReadStatus::Fresh :
            status == AXS15231B_Touch::ReadStatus::Unchanged ? TouchReadStatus::Unchanged : TouchReadStatus::Error;
        snapshot.count = polling.isPressed() ? 1 : 0;
        return snapshot;
    };
    Wire.packet({0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02});
    const auto startup_snapshot = read_snapshot();
    assert(startup_snapshot.status == TouchReadStatus::Fresh && startup_snapshot.count == 0);
    assert(snapshot_filter.update(startup_snapshot, 0).count == 0 && !snapshot_filter.canceled);
    Wire.packet({0, 1, 0, 100, 0, 80, 0, 0});
    assert(snapshot_filter.update(read_snapshot(), 0).count == 1);
    Wire.packet({0xff, 0xff, 0xff, 0xff, 0xff, 0x2b, 0x29, 0x29});
    mock_now = 10;
    auto malformed_snapshot = read_snapshot();
    assert(malformed_snapshot.status == TouchReadStatus::Error && malformed_snapshot.count == 1);
    assert(snapshot_filter.update(malformed_snapshot, 10).count == 1);
    Wire.packet({0, 1, 0x80, 130, 0, 90, 0, 0});
    mock_now = 20;
    assert(snapshot_filter.update(read_snapshot(), 20).count == 1);
    uint16_t recovered_x = 0, recovered_y = 0;
    polling.readData(&recovered_x, &recovered_y);
    assert(recovered_x == 130 && recovered_y == 90);
    Wire.packet({0, 1, 0x40, 130, 0, 90, 0, 0});
    assert(snapshot_filter.update(read_snapshot(), 30).count == 0);
    Wire.packet({0, 1, 0, 100, 0, 80, 0, 0});
    assert(snapshot_filter.update(read_snapshot(), 40).count == 1);
    Wire.packet({0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    for (unsigned now = 50; now <= 200; now += 10) {
        mock_now = now;
        const auto snapshot = read_snapshot();
        assert(snapshot.status == (now <= 150 ? TouchReadStatus::Error : TouchReadStatus::Fresh));
        assert(snapshot_filter.update(snapshot, now).count == (now < 150 ? 1 : 0));
        assert(snapshot_filter.canceled == (now == 150));
    }
    Wire.packet({0, 1, 0, 150, 0, 90, 0, 0});
    assert(snapshot_filter.update(read_snapshot(), 210).count == 1);
    Wire.packet({0, 1, 0xc0, 150, 0, 90, 0, 0});
    assert(snapshot_filter.update(read_snapshot(), 220).count == 0);
    Wire.packet({0, 1, 0x80, 150, 0, 90, 0, 0});
    assert(snapshot_filter.update(read_snapshot(), 230).count == 0);
    unsigned timeline = 240;
    const unsigned idle_warnings = warning_count;
    const uint8_t idle_values[] = {0x02, 0x29, 0xff};
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        Wire.packet({0, 1, 0, 100, 0, 80, 0, 0});
        assert(snapshot_filter.update(read_snapshot(), timeline).count == 1);
        timeline += 10;
        const uint8_t idle_value = idle_values[cycle];
        Wire.packet({idle_value, idle_value, idle_value, idle_value, idle_value, idle_value, idle_value, idle_value});
        for (unsigned idle = 0; idle < 500; ++idle) {
            mock_now = timeline;
            const auto snapshot = read_snapshot();
            assert(snapshot.status == (idle <= 10 ? TouchReadStatus::Error : TouchReadStatus::Fresh));
            assert(snapshot_filter.update(snapshot, timeline).count == (idle < 10 ? 1 : 0));
            assert(snapshot_filter.canceled == (idle == 10));
            timeline += 10;
        }
        Wire.packet({0, 1, 0x80, 100, 0, 80, 0, 0});
        assert(snapshot_filter.update(read_snapshot(), timeline).count == 0);
        timeline += 10;
    }
    assert(warning_count == idle_warnings);
    Wire.packet({0, 2, 0, 100, 0, 80, 0, 0});
    auto ignored_snapshot = read_snapshot();
    assert(ignored_snapshot.status == TouchReadStatus::Fresh && ignored_snapshot.count == 0);
    Wire.packet({0x29, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29, 0x28});
    ignored_snapshot = read_snapshot();
    assert(ignored_snapshot.status == TouchReadStatus::Fresh && ignored_snapshot.count == 0);
    Wire.packet({0xff, 0xff, 0x00, 0x00, 0x01, 0x80, 0xbe, 0x00});
    ignored_snapshot = read_snapshot();
    assert(ignored_snapshot.status == TouchReadStatus::Fresh && ignored_snapshot.count == 0);
    assert(warning_count == idle_warnings);
    Wire.result = 4;
    assert(read_snapshot().status == TouchReadStatus::Error);
    assert(last_warning.find("stage=endTransmission actual=4 expected=0") != std::string::npos);
    const unsigned warnings = warning_count;
    assert(warnings == idle_warnings + 1);
    assert(read_snapshot().status == TouchReadStatus::Error);
    assert(warning_count == warnings);
    mock_now = 5000;
    assert(read_snapshot().status == TouchReadStatus::Error);
    assert(warning_count == warnings + 1);
    assert(last_warning.find("stage=endTransmission actual=4 expected=0") != std::string::npos);
    Wire.packet({0, 1, 0, 100, 0, 80, 0, 0});
    Wire.requested_size = 0;
    mock_now = 10000;
    assert(read_snapshot().status == TouchReadStatus::Error);
    assert(last_warning.find("stage=requestFrom actual=0 expected=8") != std::string::npos);

    Wire.packet({0, 1, 0, 100, 0, 80, 0, 0});
    assert(snapshot_filter.update(read_snapshot(), timeline).count == 1);
    Wire.result = 4;
    assert(snapshot_filter.update(read_snapshot(), timeline + 10).count == 1);
    assert(snapshot_filter.update(read_snapshot(), timeline + 110).count == 0 && snapshot_filter.canceled);
    Wire.packet({0x29, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29});
    mock_now = timeline + 120;
    assert(snapshot_filter.update(read_snapshot(), timeline + 120).count == 0 && !snapshot_filter.canceled);
    mock_now = timeline + 220;
    assert(snapshot_filter.update(read_snapshot(), timeline + 220).count == 0 && !snapshot_filter.canceled);
    mock_now = timeline + 230;
    assert(snapshot_filter.update(read_snapshot(), timeline + 230).count == 0 && !snapshot_filter.canceled);
    Wire.packet({0, 1, 0, 100, 0, 80, 0, 0});
    assert(snapshot_filter.update(read_snapshot(), timeline + 240).count == 1);
}
''',
    "axs_multi": r'''
#undef MAX_AXS15231B_CONTACTS
#define MAX_AXS15231B_CONTACTS 2
#include "drivers/axs15231b/vendor/AXS15231B_touch.cpp"
#include "drivers/axs15231b_touch_driver.cpp"
int main() {
    Wire.packet_padding = 14;
    AXS15231B_TouchDriver driver;
    assert(driver.contactCapacity() == 2);
    driver.init();
    driver.setCalibration(0, 319, 0, 239);
    Wire.packet({0, 1, 0, 140, 0, 157, 0, 0});
    mock_irq();
    auto snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Fresh && snapshot.count == 1);
    Wire.packet({0, 2, 0x80, 140, 0, 157, 0x39, 0x3a, 0, 214, 0x10, 200, 0x3a, 0x3b});
    snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Fresh && snapshot.count == 2);
    assert(snapshot.contacts[0].id == 0 && snapshot.contacts[1].id == 1);
    assert(snapshot.contacts[0].horizontal == 140 && snapshot.contacts[0].vertical == 157);
    assert(snapshot.contacts[1].horizontal == 214 && snapshot.contacts[1].vertical == 200);
    Wire.packet({0, 1, 0x80, 214, 0x10, 200, 0, 0});
    snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Fresh && snapshot.count == 1);
    assert(snapshot.contacts[0].id == 1 && snapshot.contacts[0].horizontal == 214);
    assert(Wire.requested_length == 14 && Wire.written[7] == 14);

    Wire.packet({0, 2, 0, 100, 0, 80, 0, 0, 0x80, 214, 0x10, 200, 0, 0});
    snapshot = driver.readSnapshot();
    assert(snapshot.count == 2 && snapshot.contacts[0].id == 1 && snapshot.contacts[1].id == 0);
    Wire.packet({0, 2, 0x80, 110, 0, 85, 0, 0, 0x80, 215, 0x10, 201, 0, 0});
    const auto sample = driver.readSample();
    assert(sample.pressed && sample.horizontal == 215 && sample.vertical == 201);
    Wire.packet({0, 2, 0x40, 215, 0x10, 201, 0, 0, 0x80, 110, 0, 85, 0, 0});
    snapshot = driver.readSnapshot();
    assert(snapshot.count == 1 && snapshot.contacts[0].id == 0);
    Wire.packet({0, 2, 0x80, 110, 0, 85, 0, 0, 0x80, 215, 0x10, 201, 0, 0});
    snapshot = driver.readSnapshot();
    assert(snapshot.count == 1 && snapshot.contacts[0].id == 0);
    Wire.packet({0, 2, 0x80, 110, 0, 85, 0, 0, 0, 215, 0x10, 201, 0, 0});
    snapshot = driver.readSnapshot();
    assert(snapshot.count == 2);
    Wire.packet({0, 2, 0x40, 110, 0, 85, 0, 0, 0x80, 215, 0x10, 201, 0, 0});
    snapshot = driver.readSnapshot();
    assert(snapshot.count == 1 && snapshot.contacts[0].id == 1);

    const uint16_t expected[4][4] = {
        {100, 80, 150, 90}, {159, 100, 149, 150},
        {219, 159, 169, 149}, {80, 219, 90, 169}
    };
    for (uint8_t rotation = 0; rotation < 4; ++rotation) {
        Wire.packet({0, 0, 0, 0, 0, 0, 0, 0});
        driver.readSnapshot();
        driver.setRotation(rotation);
        Wire.packet({0, 2, 0, 100, 0, 80, 0, 0, 0, 150, 0x10, 90, 0, 0});
        mock_irq();
        snapshot = driver.readSnapshot();
        assert(snapshot.status == TouchReadStatus::Fresh && snapshot.count == 2);
        assert(snapshot.contacts[0].horizontal == expected[rotation][0]);
        assert(snapshot.contacts[0].vertical == expected[rotation][1]);
        assert(snapshot.contacts[1].horizontal == expected[rotation][2]);
        assert(snapshot.contacts[1].vertical == expected[rotation][3]);
    }
    driver.setRotation(0);
    Wire.packet({0, 2, 0x80, 100, 0, 80, 0, 0, 0x80, 150, 0x10, 90, 0, 0});
    TouchSnapshotFilter filter;
    mock_now = 0;
    assert(filter.update(driver.readSnapshot(), mock_now).count == 2);
    Wire.packet({0, 2, 0x80, 120, 0, 85, 0, 0, 0x80, 150, 0, 90, 0, 0});
    mock_now = 10;
    snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Error && snapshot.count == 2);
    assert(snapshot.contacts[0].horizontal == 100 && snapshot.contacts[1].id == 1);
    assert(filter.update(snapshot, mock_now).count == 2);
    Wire.packet({0, 2, 0x80, 120, 0, 85, 0, 0, 0xa0, 150, 0x10, 90, 0, 0});
    mock_now = 20;
    snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Error && snapshot.contacts[0].horizontal == 100);
    assert(filter.update(snapshot, mock_now).count == 2);
    Wire.packet({0, 2, 0x80, 120, 0, 85, 0, 0, 0x80, 150, 0x10, 90, 0, 0});
    mock_now = 30;
    snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Fresh && snapshot.contacts[0].horizontal == 120);
    assert(filter.update(snapshot, mock_now).count == 2);
    Wire.packet({0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    mock_now = 40;
    snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Error && snapshot.count == 2);
    assert(filter.update(snapshot, mock_now).count == 2);
    mock_now = 140;
    snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Error && snapshot.count == 0);
    assert(filter.update(snapshot, mock_now).count == 0 && filter.canceled);
    mock_now = 150;
    snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Fresh && snapshot.count == 0);
    assert(filter.update(snapshot, mock_now).count == 0 && !filter.canceled);
    Wire.packet({0, 2, 0x80, 120, 0, 85, 0, 0, 0x80, 150, 0x10, 90, 0, 0});
    mock_now = 160;
    snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Unchanged && snapshot.count == 0);
    assert(filter.update(snapshot, mock_now).count == 0);
    Wire.packet({0, 2, 0, 120, 0, 85, 0, 0, 0, 150, 0x10, 90, 0, 0});
    mock_irq();
    mock_now = 170;
    assert(filter.update(driver.readSnapshot(), mock_now).count == 2);
    Wire.requested_size = 8;
    mock_now = 180;
    snapshot = driver.readSnapshot();
    assert(snapshot.status == TouchReadStatus::Error && snapshot.count == 2);
    assert(filter.update(snapshot, mock_now).count == 2);
    mock_now = 280;
    assert(filter.update(driver.readSnapshot(), mock_now).count == 0 && filter.canceled);
    Wire.packet({0, 0, 0, 0, 0, 0, 0, 0});
    assert(filter.update(driver.readSnapshot(), 290).count == 0);
    Wire.packet({0, 1, 0, 100, 0x10, 80, 0, 0});
    mock_irq();
    assert(filter.update(driver.readSnapshot(), 300).count == 1);

    AXS15231B_Touch polling(22, 21, 0xff, 0x3b, 0, 2);
    assert(polling.begin());
    const auto malformed_burst = [&](AXS15231B_Touch& controller) {
        Wire.packet({0, 1, 0, 100, 0, 80, 0, 0});
        assert(controller.readSnapshot().status == TouchReadStatus::Fresh);
        Wire.packet({0x29, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29});
        const auto malformed = controller.readSnapshot();
        assert(malformed.status == TouchReadStatus::Error && malformed.count == 1);
    };
    const unsigned initial_logs = invalid_report_log_count;
    mock_now = 0;
    malformed_burst(polling);
    assert(invalid_report_log_count == initial_logs + 1);
    for (unsigned now = 20; now < 5000; now += 20) {
        mock_now = now;
        malformed_burst(polling);
    }
    mock_now = 4999;
    malformed_burst(polling);
    assert(invalid_report_log_count == initial_logs + 1);
    mock_now = 5000;
    malformed_burst(polling);
    assert(invalid_report_log_count == initial_logs + 2);
    mock_now = 5010;
    assert(polling.readSnapshot().count == 1);
    mock_now = 5100;
    assert(polling.readSnapshot().count == 0);
    assert(invalid_report_log_count == initial_logs + 2);

    AXS15231B_Touch rollover(22, 21, 0xff, 0x3b, 0, 2);
    assert(rollover.begin());
    mock_now = UINT32_MAX - 1000;
    malformed_burst(rollover);
    assert(invalid_report_log_count == initial_logs + 3);
    mock_now = 3998;
    malformed_burst(rollover);
    assert(invalid_report_log_count == initial_logs + 3);
    mock_now = 3999;
    malformed_burst(rollover);
    assert(invalid_report_log_count == initial_logs + 4);
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