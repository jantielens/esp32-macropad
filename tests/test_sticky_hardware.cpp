#include "../src/app/board_config.h"
#include "board_hardware.h"
#include "drivers/gt911_touch_driver.h"
#include <SPI.h>
#include <driver/gpio.h>
#include <cstdio>
#include <vector>
#include <utility>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)
TwoWire Wire(0), Wire1(1);
static std::vector<std::pair<int, int>> outputs;
static std::vector<unsigned long> waits;
static int failPin = -1;
static bool locked = false, failLock = false;
static uint32_t clockMs = 0;
extern "C" unsigned long millis() { return clockMs; }
void delay(unsigned long ms) { waits.push_back(ms); clockMs += ms; }
void pinMode(int, int) {}
void digitalWrite(int, int) {}
esp_err_t gpio_set_level(gpio_num_t pin, int level) {
    if (pin == failPin) return 7;
    outputs.emplace_back(pin, level);
    return ESP_OK;
}
esp_err_t gpio_set_direction(gpio_num_t pin, int) { return pin == failPin ? 7 : ESP_OK; }
void i2c_bus_init() {}
bool i2c_bus_lock(TickType_t) { if (failLock) return false; CHECK(!locked); locked = true; return true; }
void i2c_bus_unlock() { CHECK(locked); locked = false; }

static void resolution(uint16_t width, uint16_t height) {
    Wire1.registers[0x8146] = width & 255;
    Wire1.registers[0x8147] = width >> 8;
    Wire1.registers[0x8148] = height & 255;
    Wire1.registers[0x8149] = height >> 8;
}
static void contact(uint16_t x, uint16_t y) {
    Wire1.registers[0x814e] = 0x81;
    Wire1.registers[0x814f] = 0;
    Wire1.registers[0x8150] = x & 255;
    Wire1.registers[0x8151] = x >> 8;
    Wire1.registers[0x8152] = y & 255;
    Wire1.registers[0x8153] = y >> 8;
}
int main() {
    static_assert(HAS_DISPLAY && HAS_TOUCH && HAS_LVGL_EPAPER && HAS_EPAPER_GRAYSCALE);
    static_assert(!IS_EPAPER_FRAME && HAS_MCP && HAS_MQTT && HAS_FILESYSTEM);
    static_assert(!HAS_AUDIO && !HAS_SOUND_PLAYER && !HAS_AUDIO_INPUT && !HAS_SD_CARD && !USE_SD_STORAGE);
    static_assert(!HAS_BLE && !HAS_BLE_HID && !HAS_USB_HID && !HAS_NATIVE_EXTENSIONS);
    static_assert(!HAS_SENSOR_BATTERY_ADC && !HAS_SENSOR_DUMMY && !HAS_SENSOR_BME280 && !HAS_SENSOR_AHT10);
    static_assert(!HAS_IMAGE_LIBRARY && !HAS_IMAGE_FETCH && HAS_CONFIG_MODE_BUTTON && BUTTON_PIN == 5);
    static_assert(NUM_HW_BUTTONS == 3 && HW_BUTTON_DEFS[0].pin == 4 && HW_BUTTON_DEFS[1].pin == 5 && HW_BUTTON_DEFS[2].pin == 6);
    static_assert(HW_BUTTON_DEFS[0].active_low && HW_BUTTON_DEFS[1].active_low && HW_BUTTON_DEFS[2].active_low);
#ifdef HW_BUTTON_DEFAULTS_JSON
#error Sticky runtime button assignments must remain empty
#endif
    failPin = 45;
    CHECK(!sticky_early_power_init());
    CHECK(outputs.empty());
    CHECK(!board_hardware_init() && Wire.begins == 0);
    failPin = -1;
    CHECK(sticky_early_power_init());
    CHECK((outputs == std::vector<std::pair<int, int>>{{45, 1}, {46, 1}}));
    failPin = 10;
    CHECK(!board_hardware_init() && Wire.begins == 0);
    failPin = -1;
    failLock = true;
    CHECK(!board_hardware_init() && !locked && Wire.begins == 0);
    failLock = false;
    Wire.failBegin = true;
    CHECK(!board_hardware_init() && !locked);
    Wire.failBegin = false;
    CHECK(board_hardware_init() && sticky_hardware_ready() && !locked);
    CHECK(Wire.begins == 2 && Wire.sda == 1 && Wire.scl == 0 && Wire.frequency == 400000);
    CHECK(Wire1.begins == 0);
    const size_t count = outputs.size();
    CHECK(board_hardware_init() && Wire.begins == 2 && outputs.size() == count);
    for (const auto& output : outputs) {
        if (output.first == 10 || output.first == 38 || output.first == 48 || output.first == 39) CHECK(output.second == 0);
        if (output.first == 8 || output.first == 15) CHECK(output.second == 1);
    }
    CHECK((waits == std::vector<unsigned long>{250, 100}));
    GT911_TouchDriver touch;
    resolution(800, 480);
    touch.init();
    CHECK(Wire1.begins == 1 && Wire1.sda == 3 && Wire1.scl == 2 && Wire1.frequency == 400000);
    CHECK(Wire.begins == 2);
    for (auto point : {std::pair<uint16_t, uint16_t>{0, 0}, {799, 0}, {0, 479}, {799, 479}, {400, 240}}) {
        contact(point.first, point.second);
        auto sample = touch.readSample();
        CHECK(sample.status == TouchReadStatus::Fresh && sample.pressed);
        CHECK(sample.horizontal == point.first && sample.vertical == point.second);
        sample = touch.readSample();
        CHECK(sample.status == TouchReadStatus::Unchanged && sample.pressed);
    }
    Wire1.registers[0x814e] = 0x80;
    CHECK(!touch.readSample().pressed);
    Wire1.failRead = true;
    CHECK(touch.readSample().status == TouchReadStatus::Error);
    Wire1.failRead = false;
    resolution(480, 800);
    touch.init();
    CHECK(Wire1.begins == 1);
    contact(479, 799);
    auto sample = touch.readSample();
    CHECK(sample.horizontal == 799 && sample.vertical == 0);
    contact(480, 799);
    CHECK(touch.readSample().status == TouchReadStatus::Error);
    CHECK(Wire1.registers[0x814e] == 0);
    if (Wire1.registers[0x814e] == 0) Wire1.registers[0x814e] = 0x80;
    sample = touch.readSample();
    CHECK(sample.status == TouchReadStatus::Fresh && !sample.pressed);
    contact(479, 799);
    sample = touch.readSample();
    CHECK(sample.status == TouchReadStatus::Fresh && sample.horizontal == 799 && sample.vertical == 0);
    resolution(123, 456);
    touch.init();
    CHECK(touch.readSample().status == TouchReadStatus::Error);
    Wire1.failRead = true;
    touch.init();
    CHECK(touch.readSample().status == TouchReadStatus::Error && Wire1.begins == 1);
    std::printf("Sticky board/touch checks: %d failures\n", failures);
    return failures ? 1 : 0;
}
