#!/usr/bin/env python3
"""Compile production StopWatch HAL against deterministic peripheral doubles."""
import pathlib
import re
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def production(path):
    return re.sub(r"^\s*#include[^\n]*", "", (ROOT / path).read_text(), flags=re.M)


PRELUDE = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <math.h>
#include <new>
#include <string>
#include <tuple>
#include <vector>
#define HAS_M5STACK_STOPWATCH true
#define HAS_SENSOR_BATTERY_ADC false
#define HAS_MQTT false
#define HAS_BLE false
#define DISPLAY_WIDTH 466
#define DISPLAY_HEIGHT 466
#define DISPLAY_ROTATION 0
#define LVGL_BUFFER_SIZE (466 * 16)
#define LCD_QSPI_CS 39
#define LCD_QSPI_PCLK 40
#define LCD_QSPI_D0 41
#define LCD_QSPI_D1 42
#define LCD_QSPI_D2 46
#define LCD_QSPI_D3 45
#define TFT_SPI_FREQ_HZ 40000000
#define INPUT_PULLUP 2
#define LOW 0
#define FALLING 3
#define IRAM_ATTR
#define RGB565_BLACK 0
#define GFX_NOT_DEFINED -1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define LOGE(...) ((void)0)
#define LOGW(...) ((void)0)
using lv_display_t = int;
static std::vector<int> delays;
static void delay(int value) { delays.push_back(value); }
static void pinMode(int pin, int mode) { assert((pin == 13 || pin == 38) && mode == INPUT_PULLUP); }
static int irq = 1;
static int digitalRead(int pin) { assert(pin == 13); return irq; }
static int digitalPinToInterrupt(int pin) { return pin; }
static void attachInterrupt(int pin, void (*)(), int mode) { assert(pin == 13 && mode == FALLING); }
static void detachInterrupt(int pin) { assert(pin == 13); }
static bool allocationFails = false;
static void* heap_caps_malloc(size_t size, int caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return allocationFails ? nullptr : malloc(size);
}
static void heap_caps_free(void* pointer) { free(pointer); }

using SemaphoreHandle_t = int*;
static int semaphore = 0, lockDepth = 0, mutexCreates = 0;
static bool mutexFails = false, lockFails = false;
static SemaphoreHandle_t xSemaphoreCreateMutex() {
    ++mutexCreates;
    return mutexFails ? nullptr : &semaphore;
}
static constexpr int pdTRUE = 1;
static int xSemaphoreTake(SemaphoreHandle_t, int) {
    if (lockFails) return 0;
    assert(lockDepth == 0);
    ++lockDepth;
    return pdTRUE;
}
static void xSemaphoreGive(SemaphoreHandle_t) { assert(lockDepth == 1); --lockDepth; }
using TickType_t = int;
#define pdMS_TO_TICKS(x) (x)
void i2c_bus_init();
bool i2c_bus_lock(TickType_t timeout = 50);
void i2c_bus_unlock();

struct MockWire {
    std::array<std::array<uint8_t, 256>, 128> regs{};
    std::vector<std::tuple<int, int, int>> writes;
    std::vector<uint8_t> tx, rx;
    int address = 0, cursor = 0, begins = 0, transactions = 0, failureAt = -1;
    bool primary = true, fallback = true, pmic = true, shortRead = false, shortWrite = false;
    bool begin(int sda, int scl, int hz) {
        assert(lockDepth == 1 && sda == 47 && scl == 48 && hz == 100000);
        ++begins; return true;
    }
    void beginTransmission(uint8_t addr) {
        assert(lockDepth == 1); address = addr; tx.clear();
    }
    size_t write(uint8_t value) {
        assert(lockDepth == 1); tx.push_back(value);
        return shortWrite ? 0 : 1;
    }
    int endTransmission(bool = true) {
        assert(lockDepth == 1);
        const int index = transactions++;
        if (index == failureAt || (address == 0x4f && !primary) ||
            (address == 0x6f && !fallback) || (address == 0x6e && !pmic)) return 4;
        if (tx.size() == 2 && !shortWrite) {
            regs[address][tx[0]] = tx[1];
            writes.emplace_back(address, tx[0], tx[1]);
        }
        return 0;
    }
    size_t requestFrom(uint8_t addr, uint8_t length) {
        assert(lockDepth == 1 && addr == address && tx.size() == 1);
        rx.clear(); cursor = 0;
        for (int i = 0; i < (shortRead ? length - 1 : length); ++i)
            rx.push_back(regs[addr][tx[0] + i]);
        return rx.size();
    }
    int available() { return rx.size() - cursor; }
    int read() {
        assert(lockDepth == 1);
        return cursor < int(rx.size()) ? rx[cursor++] : -1;
    }
} Wire;

static std::vector<std::pair<int,int>> commands;
static std::vector<uint16_t> drawn;
static std::array<int,4> rectangle{}, offsets{};
static bool gfxBegins = true;
static int panelBrightness = -1;
class Arduino_DataBus {
public:
    virtual ~Arduino_DataBus() = default;
    void sendCommand(int command) { commands.emplace_back(command, -1); }
    void beginWrite() {}
    void endWrite() {}
    void writeCommand(int command) { sendCommand(command); }
    void writeC8D8(int command, int value) { commands.emplace_back(command, value); }
    void writeC8D16(int command, int value) { commands.emplace_back(command, value); }
};
class Arduino_ESP32QSPI : public Arduino_DataBus {
public:
    Arduino_ESP32QSPI(int cs, int clk, int d0, int d1, int d2, int d3) {
        assert(cs == 39 && clk == 40 && d0 == 41 && d1 == 42 && d2 == 46 && d3 == 45);
    }
};
class Arduino_CO5300 {
protected:
    Arduino_DataBus* _bus;
    virtual void tftInit() {}
public:
    Arduino_CO5300(Arduino_DataBus* bus, int rst, int rotation, int w, int h,
                   int x1, int y1, int x2, int y2) : _bus(bus) {
        assert(rst == -1 && rotation == 0 && w == 466 && h == 466);
        offsets = {x1,y1,x2,y2};
    }
    virtual ~Arduino_CO5300() = default;
    bool begin(int hz) {
        assert(hz == 40000000);
        if (!gfxBegins) return false;
        tftInit(); return true;
    }
    void fillScreen(int color) { assert(color == 0); }
    void setBrightness(int value) { panelBrightness = value; }
    void draw16bitRGBBitmap(int x, int y, uint16_t* pixels, int w, int h) {
        rectangle = {x,y,w,h};
        drawn.assign(pixels, pixels + w*h);
    }
};

struct JsonValue {
    bool null = true;
    double value = 0;
    template<typename T> JsonValue& operator=(T v) { null = false; value = v; return *this; }
    JsonValue& operator=(std::nullptr_t) { null = true; return *this; }
};
struct JsonObject {
    std::map<std::string, JsonValue> values;
    JsonValue& operator[](const char* key) { return values[key]; }
};
struct SensorCallbacks {
    const char* name = nullptr;
    void (*init)() = nullptr;
    void (*append_api)(JsonObject&) = nullptr;
    void (*append_mqtt)(JsonObject&) = nullptr;
};
class SensorRegistry {
public:
    SensorCallbacks callbacks;
    bool add(const SensorCallbacks& value) { callbacks = value; return true; }
};
static void sensor_manager_set_number(JsonObject& doc, const char* key, float value, bool valid) {
    if (valid) doc[key] = value; else doc[key] = nullptr;
}
static void sensor_manager_set_bool(JsonObject& doc, const char* key, bool value, bool valid) {
    if (valid) doc[key] = value; else doc[key] = nullptr;
}
'''

BODY = "\n".join(production(path) for path in [
    "src/app/touch_sample.h",
    "src/app/touch_driver.h",
    "src/app/display_driver.h",
    "src/app/m5stack_stopwatch.h",
    "src/app/i2c_bus.cpp",
    "src/app/m5stack_stopwatch.cpp",
    "src/app/drivers/wire_cst820b_touch_driver.h",
    "src/app/drivers/wire_cst820b_touch_driver.cpp",
    "src/app/drivers/arduino_gfx_co5300_driver.h",
    "src/app/drivers/arduino_gfx_co5300_driver.cpp",
    "src/app/sensors/battery_adc_sensor.cpp",
    "src/app/sensors/stopwatch_battery_sensor.cpp",
])

TESTS = r'''
static void reset() {
    Wire = MockWire{};
    expander = 0; attempted = ready = audioPowered = false;
    delays.clear(); commands.clear(); drawn.clear();
    allocationFails = false; gfxBegins = true; irq = 1;
    lockFails = mutexFails = false;
    // Preserve unrelated outputs and PMIC configuration to exercise RMW.
    Wire.regs[0x4f][0x05] = 0xff;
    Wire.regs[0x4f][0x06] = 0xff;
    Wire.regs[0x4f][0x13] = 0xff;
    Wire.regs[0x4f][0x14] = 0xff;
    Wire.regs[0x6e][0x06] = 0xff;
    Wire.regs[0x6e][0x0b] = 0xa5;
    Wire.regs[0x6e][0x11] = 0x08; // CHG_PROG programming left untouched.
    assert(lockDepth == 0);
}
static void battery(int vbat, int vin, uint8_t gpio) {
    Wire.regs[0x6e][0x22] = vbat;
    Wire.regs[0x6e][0x23] = vbat >> 8;
    Wire.regs[0x6e][0x24] = vin;
    Wire.regs[0x6e][0x25] = vin >> 8;
    Wire.regs[0x6e][0x12] = gpio;
}
static void contact(int count, int event, int x, int y) {
    Wire.regs[0x15][2] = count;
    Wire.regs[0x15][3] = (event << 6) | (x >> 8);
    Wire.regs[0x15][4] = x;
    Wire.regs[0x15][5] = y >> 8;
    Wire.regs[0x15][6] = y;
}
int main() {
    mutexFails = true;
    assert(!m5stack_stopwatch_init() && !attempted && Wire.begins == 0);
    mutexFails = false;
    i2c_bus_init();
    const int created = mutexCreates;
    i2c_bus_init();
    assert(mutexCreates == created);
    reset();
    assert(m5stack_stopwatch_init());
    assert(delays == std::vector<int>({10,10,50}));
    assert(Wire.regs[0x4f][5] == 0xfb); // audio OFF, USB/OLED/reset HIGH.
    assert(Wire.regs[0x4f][6] == 0xfc); // PA and motor OFF.
    assert(Wire.regs[0x4f][3] == 0x9d);
    assert(Wire.regs[0x4f][4] == 3);
    assert(Wire.regs[0x4f][0x13] == 0x62);
    assert(Wire.regs[0x4f][0x14] == 0xfc);
    assert(Wire.regs[0x6e][6] == 0xf7); // boost OFF; other bits preserved.
    assert(Wire.regs[0x6e][0x0b] == 0xa5 && Wire.regs[0x6e][0x11] == 8);
    const auto successfulWrites = Wire.writes;
    const int count = Wire.transactions;
    assert(m5stack_stopwatch_init() && Wire.begins == 1 && Wire.transactions == count);
    // Confirm the power/reset waveform rather than only its final state.
    std::vector<int> resetLatch;
    for (auto [address, reg, value] : successfulWrites)
        if (address == 0x4f && reg == 5) resetLatch.push_back(value);
    assert(resetLatch == std::vector<int>({0xfb,0xfb,0xe3,0xfb}));
    assert(!m5stack_stopwatch_audio_mute(false)); // Cannot enable unpowered PA.
    assert(m5stack_stopwatch_audio_power(true));
    assert((Wire.regs[0x4f][5] & 4) && !(Wire.regs[0x4f][6] & 2));
    assert(m5stack_stopwatch_audio_mute(false) && (Wire.regs[0x4f][6] & 2));
    assert(m5stack_stopwatch_audio_power(false));
    assert(!(Wire.regs[0x4f][5] & 4) && !(Wire.regs[0x4f][6] & 3));
    assert(m5stack_stopwatch_audio_power(true));
    Wire.failureAt = Wire.transactions;
    assert(!m5stack_stopwatch_audio_power(false)); // Failed mute still cuts rail.
    assert(!(Wire.regs[0x4f][5] & 4));
    for (int fail = 1; fail < count; ++fail) {
        reset(); Wire.failureAt = fail;
        assert(!m5stack_stopwatch_init() && lockDepth == 0 && !ready);
        assert(!(Wire.regs[0x4f][5] & 0x9c) && !(Wire.regs[0x4f][6] & 3));
        assert(!m5stack_stopwatch_audio_power(true));
        const int failedCount = Wire.transactions;
        assert(!m5stack_stopwatch_init() && Wire.transactions == failedCount && Wire.begins == 1);
    }
    reset(); Wire.primary = false;
    assert(m5stack_stopwatch_init() && expander == 0x6f);
    reset(); Wire.primary = Wire.fallback = false;
    assert(!m5stack_stopwatch_init() && !ready);
    reset(); Wire.shortRead = true;
    assert(!m5stack_stopwatch_init() && !ready && lockDepth == 0);
    reset(); Wire.shortWrite = true;
    assert(!m5stack_stopwatch_init() && !ready && lockDepth == 0);
    reset(); assert(m5stack_stopwatch_init());
    lockFails = true;
    const int beforeLock = Wire.transactions;
    uint8_t touch[5]; uint16_t millivolts = 0; bool charging = false;
    assert(!m5stack_stopwatch_ready());
    assert(!m5stack_stopwatch_audio_power(true));
    assert(!m5stack_stopwatch_touch_read(touch, 5));
    assert(!m5stack_stopwatch_battery_read(millivolts, charging));
    assert(Wire.transactions == beforeLock && lockDepth == 0);
    lockFails = false;
    battery(3897, 5000, 0);
    assert(m5stack_stopwatch_battery_read(millivolts, charging) && millivolts == 3897 && charging);
    battery(3897, 5000, 4);
    assert(m5stack_stopwatch_battery_read(millivolts, charging) && !charging);
    battery(3897, 3999, 0);
    assert(m5stack_stopwatch_battery_read(millivolts, charging) && !charging);
    Wire.shortRead = true;
    assert(!m5stack_stopwatch_battery_read(millivolts, charging));
    Wire.shortRead = false;

    SensorRegistry registry;
    register_stopwatch_battery_sensor(registry);
    assert(registry.callbacks.append_api && registry.callbacks.append_mqtt);
    JsonObject doc;
    battery(4200, 5000, 0);
    registry.callbacks.append_api(doc);
    assert(doc["battery_percentage"].value == 100 && doc["battery_charging"].value == 1);
    battery(2000, 5000, 0);
    registry.callbacks.append_mqtt(doc);
    assert(doc["battery_voltage"].null && doc["battery_percentage"].null && doc["battery_charging"].null);

    Wire_CST820B_TouchDriver driver;
    driver.init();
    const int beforeTouch = Wire.transactions;
    assert(driver.readSample().status == TouchReadStatus::Unchanged && Wire.transactions == beforeTouch);
    irq = 0;
    for (int rotation = 0; rotation < 4; ++rotation) {
        driver.setRotation(rotation);
        contact(1, 0, 233, 0);
        const auto sample = driver.readSample();
        assert(sample.status == TouchReadStatus::Fresh && sample.pressed);
        const std::array<std::pair<int,int>,4> expected{{{465,0},{0,0},{0,465},{465,465}}};
        assert(sample.horizontal == expected[rotation].first && sample.vertical == expected[rotation].second);
    }
    contact(1, 3, 0, 0);
    assert(driver.readSample().status == TouchReadStatus::Error);
    contact(2, 0, 0, 0);
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.shortRead = true;
    assert(driver.readSample().status == TouchReadStatus::Error);
    Wire.shortRead = false;
    irq = 1; contact(0, 1, 0, 0);
    auto release = driver.readSample(); // Read release even though interrupt deasserted.
    assert(release.status == TouchReadStatus::Fresh && !release.pressed);
    contact(1, 0, 0, 0); cst820b_interrupt(); // Capture an IRQ pulse between polls.
    assert(driver.readSample().pressed);
    irq = 0; contact(1, 2, 4095, 4095); driver.setRotation(0);
    auto clamped = driver.readSample();
    assert(clamped.horizontal == 465 && clamped.vertical == 465);
    driver.setCalibration(10,10,10,10); // Invalid calibration must not divide by zero.
    driver.setCalibration(0,100,0,100);
    contact(1, 0, 100, 100);
    auto calibrated = driver.readSample();
    assert(calibrated.horizontal == 465 && calibrated.vertical == 465);
    assert(Wire.begins == 1);

    Arduino_GFX_CO5300_Driver display;
    display.init();
    assert((display.isAvailable() && offsets == std::array<int,4>({7,0,7,0})));
    const std::vector<std::pair<int,int>> expectedCommands{
        {0x11,-1},{0xc4,0x80},{0x35,0x80},{0x44,0x1d2},{0x3a,0x55},
        {0x53,0x20},{0x20,-1},{0x36,0},{0x51,0},{0x29,-1}};
    assert(commands == expectedCommands);
    display.setBacklightBrightness(50); assert(panelBrightness == 127);
    display.setBacklight(false); assert(panelBrightness == 0);
    display.setBacklight(true); assert(panelBrightness == 127);
    const uint16_t pixels[] = {1,2,99,3,4,99,5,6,99};
    display.flushSrcStride = 6;
    for (int rotation = 0; rotation < 4; ++rotation) {
        display.setRotation(rotation);
        display.setAddrWindow(10,20,2,3);
        display.pushColors(const_cast<uint16_t*>(pixels),6);
        const std::array<std::array<int,4>,4> windows{{
            {10,20,2,3},{443,10,3,2},{454,443,2,3},{20,454,3,2}}};
        const std::array<std::vector<uint16_t>,4> values{{
            {1,2,3,4,5,6},{5,3,1,6,4,2},{6,5,4,3,2,1},{2,4,6,1,3,5}}};
        assert(rectangle == windows[rotation] && drawn == values[rotation]);
    }
    drawn.clear();
    display.setAddrWindow(465,465,2,2);
    display.pushColors(const_cast<uint16_t*>(pixels),4);
    assert(drawn.empty());
    commands.clear(); delays.clear();
    display.displaySleep(); display.displayWakeSleepOut();
    assert(delays.empty()); // Two-phase methods must not block LVGL lock.
    display.displayWakeDisplayOn();
    assert((commands == std::vector<std::pair<int,int>>({{0x28,-1},{0x10,-1},{0x11,-1},{0x29,-1}})));
    allocationFails = true;
    Arduino_GFX_CO5300_Driver failedDisplay;
    failedDisplay.init(); assert(!failedDisplay.isAvailable());
    allocationFails = false; gfxBegins = false;
    Arduino_GFX_CO5300_Driver failedBus;
    failedBus.init(); assert(!failedBus.isAvailable());
    assert(lockDepth == 0);
}
'''

directory = ROOT / "build" / "stopwatch-hal-check"
directory.mkdir(parents=True, exist_ok=True)
try:
    source = directory / "check.cpp"
    executable = directory / "check"
    source.write_text(PRELUDE + BODY.replace("#pragma once", "") + TESTS)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
finally:
    shutil.rmtree(directory)
print("PASS: StopWatch power sequence, failure paths, locking, PMIC sensors, CST820B, CO5300 geometry/lifecycle")
