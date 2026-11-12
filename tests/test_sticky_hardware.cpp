#include "../src/app/board_config.h"
#include "board_hardware.h"
#include "drivers/gt911_touch_driver.h"
#include "../src/app/presented_frame.h"
#include <SPI.h>
#include <driver/gpio.h>
#include <cstdio>
#include <vector>
#include <utility>
#include <cmath>
#include <string>
#include "../src/boards/reterminal-sticky-interactive/drivers/sticky_sensors.cpp"
#define MQTT_MANAGER_H
class MqttManager {
public:
    const char *baseTopic() const { return "test"; }
    bool connected() { return true; }
    bool publishImmediate(const char *, const char *, bool) { return true; }
};
MqttManager mqtt_manager;
#include "../src/app/sensors/sensor_manager.cpp"

bool ledcAttach(uint8_t pin, uint32_t frequency, uint8_t resolution);
bool ledcWrite(uint8_t pin, uint32_t duty);
uint32_t ledcChangeFrequency(uint8_t pin, uint32_t frequency, uint8_t resolution);
bool ledcDetach(uint8_t pin);
#include "../src/boards/reterminal-sticky-interactive/drivers/sticky_buzzer.cpp"

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)
TwoWire Wire(0), Wire1(1);
static std::vector<std::pair<int, int>> outputs;
static std::vector<unsigned long> waits;
static int failPin = -1;
static bool locked = false, failLock = false;
static uint32_t clockMs = 0;
static bool otaActive = false;
int shtTestError = 1;
float shtTestTemperature = 21.5f, shtTestHumidity = 50;
unsigned shtTestSamples = 0;
bool shtTestLockHeld = false;
bool sticky_test_unlocked_wire0 = false;
bool sticky_test_invalid_gauge_transaction = false;
static std::vector<std::string> discoveryKeys;
static int externalPower = 0;
static uint32_t buzzerDuty = 0, buzzerFrequency = 0;
static bool buzzerAttached = false, failFrequency = false, failSilence = false, failDetach = false;
bool ledcAttach(uint8_t pin, uint32_t frequency, uint8_t resolution) {
    CHECK(pin == 48 && resolution == 10);
    buzzerAttached = true;
    buzzerFrequency = frequency;
    return true;
}
bool ledcWrite(uint8_t pin, uint32_t duty) {
    CHECK(pin == 48 && buzzerAttached);
    if (!duty && failSilence) return false;
    buzzerDuty = duty;
    return true;
}
uint32_t ledcChangeFrequency(uint8_t pin, uint32_t frequency, uint8_t resolution) {
    CHECK(pin == 48 && buzzerAttached && resolution == 10);
    if (failFrequency) return 0;
    buzzerFrequency = frequency;
    return frequency;
}
bool ledcDetach(uint8_t pin) {
    CHECK(pin == 48 && buzzerAttached);
    if (failDetach) return false;
    buzzerAttached = false;
    return true;
}
extern "C" unsigned long millis() { return clockMs; }
void delay(unsigned long ms) { waits.push_back(ms); clockMs += ms; }
void pinMode(int, int) {}
void digitalWrite(int pin, int level) { if (pin == 48 && level == LOW) buzzerDuty = 0; }
int digitalRead(int pin) { CHECK(pin == 9); return externalPower; }
bool ota_activity_is_active() { return otaActive; }
void sensor_manager_register_all(SensorRegistry &registry) { board_register_sensors(registry); }
bool ha_discovery_publish_sensor_config(MqttManager &, const char *key, const char *, const char *, const char *, const char *, const char *, const char *, bool) { discoveryKeys.emplace_back(key); return true; }
bool ha_discovery_publish_binary_sensor_config(MqttManager &, const char *key, const char *, const char *, const char *, const char *, const char *) { discoveryKeys.emplace_back(key); return true; }
esp_err_t gpio_set_level(gpio_num_t pin, int level) {
    if (pin == failPin) return 7;
    outputs.emplace_back(pin, level);
    return ESP_OK;
}
esp_err_t gpio_set_direction(gpio_num_t pin, int) { return pin == failPin ? 7 : ESP_OK; }
void i2c_bus_init() {}
bool i2c_bus_lock(TickType_t) { if (failLock) return false; CHECK(!locked); locked = true; return true; }
void i2c_bus_unlock() { CHECK(locked); locked = false; }
bool sticky_test_bus_locked() { return locked; }

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
    static_assert(HAS_AUDIO && !HAS_SOUND_PLAYER && !HAS_AUDIO_INPUT && !HAS_SD_CARD && !USE_SD_STORAGE);
    static_assert(AUDIO_OUTPUT_DRIVER == AUDIO_OUTPUT_DRIVER_STICKY_PWM);
    static_assert(!HAS_BLE && !HAS_BLE_HID && !HAS_USB_HID && !HAS_NATIVE_EXTENSIONS);
    static_assert(!HAS_SENSOR_BATTERY_ADC && !HAS_SENSOR_DUMMY && !HAS_SENSOR_BME280 && !HAS_SENSOR_AHT10);
    static_assert(!HAS_IMAGE_LIBRARY && !HAS_IMAGE_FETCH && HAS_CONFIG_MODE_BUTTON && BUTTON_PIN == 5);
    static_assert(NUM_HW_BUTTONS == 3 && HW_BUTTON_DEFS[0].pin == 4 && HW_BUTTON_DEFS[1].pin == 5 && HW_BUTTON_DEFS[2].pin == 6);
    static_assert(HW_BUTTON_DEFS[0].active_low && HW_BUTTON_DEFS[1].active_low && HW_BUTTON_DEFS[2].active_low);
#ifdef HW_BUTTON_DEFAULTS_JSON
#error Sticky runtime button assignments must remain empty
#endif
    AudioOutputDriver* buzzer = audio_output_driver_create();
    CHECK(buzzer->begin(48000) && buzzerDuty == 0 && buzzer->supportsNativeTone());
    CHECK(!buzzer->supportsMute() && buzzer->needsRebootShutdown());
    CHECK(buzzer->setVolume(100) && buzzer->setNativeTone(1000, 100));
    CHECK(buzzerFrequency == 1000 && buzzerDuty == 512);
    CHECK(buzzer->setNativeTone(500, 15) && buzzerFrequency == 500 && buzzerDuty == 76);
    CHECK(buzzer->setVolume(0) && buzzerDuty == 0);
    CHECK(buzzer->setVolume(50) && buzzerDuty == 38);
    CHECK(buzzer->setNativeTone(0, 100) && buzzerDuty == 0);
    CHECK(buzzer->setNativeTone(500, 100) && buzzerDuty == 256);
    failFrequency = true;
    CHECK(!buzzer->setNativeTone(1000, 100) && buzzerDuty == 0);
    failFrequency = false;
    CHECK(buzzer->end() && !buzzerAttached);
    for (const bool cleanupFails : {false, true}) {
        CHECK(buzzer->begin(48000));
        CHECK(buzzer->setNativeTone(1000, 100) && buzzerDuty != 0);
        failSilence = true;
        failDetach = cleanupFails;
        CHECK(!buzzer->setNativeTone(0, 0));
        CHECK(buzzerAttached == cleanupFails && buzzer->isAvailable() == cleanupFails);
        if (cleanupFails) {
            CHECK(buzzerDuty != 0);
            failDetach = false;
            CHECK(buzzer->end());
        }
        CHECK(!buzzerAttached && buzzerDuty == 0);
        failSilence = false;
    }
    failPin = 45;
    CHECK(!sticky_early_power_init());
    CHECK(outputs.empty());
    CHECK(!board_hardware_init() && Wire.begins == 0);
    sensor_manager_init();
    sensor_manager_sample_once();
    JsonDocument sensorJson;
    JsonObject sensorObject = sensorJson.to<JsonObject>();
    sensor_manager_append_api(sensorObject);
    CHECK(sensorJson["temperature"].isNull() && sensorJson["humidity"].isNull());
    CHECK(sensorJson["battery_voltage"].isNull() && sensorJson["battery_percentage"].isNull());
    CHECK(sensorJson["external_power_present"].isNull());
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
    Wire.failWrite = true;
    CHECK(shtTestLockHeld && sensor_manager_field_count() == 5);
    CHECK(std::string(sensor_manager_field_at(2)->key) == "battery_voltage");
    CHECK(std::string(sensor_manager_field_at(2)->unit) == "V");
    CHECK(std::string(sensor_manager_field_at(3)->key) == "battery_percentage");
    CHECK(std::string(sensor_manager_field_at(3)->unit) == "%");
    const unsigned beforeDiscovery = Wire.transactions;
    sensor_manager_publish_ha_discovery(mqtt_manager);
    CHECK((discoveryKeys == std::vector<std::string>{"temperature", "humidity", "battery_voltage", "battery_percentage", "external_power_present"}));
    CHECK(Wire.transactions == beforeDiscovery);
    clockMs += 5000;
    sensor_manager_loop();
    sensor_manager_append_api(sensorObject);
    CHECK(sensorJson["temperature"].isNull() && sensorJson["humidity"].isNull());
    CHECK(sensorJson["battery_voltage"].isNull() && sensorJson["battery_percentage"].isNull());
    CHECK(sensorJson["external_power_present"].as<bool>() == false);
    Wire.failWrite = false;
    Wire.registers[0x08] = 0xd9; Wire.registers[0x09] = 0x0e;
    Wire.registers[0x2c] = 73; Wire.registers[0x2d] = 0;
    shtTestError = 0;
    externalPower = 1;
    clockMs += 5000;
    sensor_manager_loop();
    sensor_manager_append_api(sensorObject);
    CHECK(std::fabs(sensorJson["battery_voltage"].as<double>() - 3.801) < 0.001);
    CHECK(sensorJson["battery_percentage"].as<int>() == 73);
    CHECK(sensorJson["temperature"].as<float>() == 21.5f && sensorJson["humidity"].as<float>() == 50);
    CHECK(sensorJson["external_power_present"].as<bool>());
    const unsigned transactions = Wire.transactions, samples = shtTestSamples;
    sensor_manager_append_api(sensorObject);
    sensor_manager_append_mqtt(sensorObject);
    double reading = 0;
    CHECK(sensor_manager_get_number("battery_voltage", &reading));
    CHECK(Wire.transactions == transactions && shtTestSamples == samples);
    otaActive = true;
    clockMs += 5000;
    sensor_manager_loop();
    CHECK(Wire.transactions == transactions && shtTestSamples == samples);
    otaActive = false;
    failLock = true;
    externalPower = 0;
    sensor_manager_loop();
    sensor_manager_append_api(sensorObject);
    CHECK(!sensorJson["external_power_present"].as<bool>());
    CHECK(std::fabs(sensorJson["battery_voltage"].as<double>() - 3.801) < 0.001);
    CHECK(Wire.transactions == transactions && shtTestSamples == samples);
    failLock = false;
    Wire.nackRegister = 0x08;
    Wire.registers[0x2c] = 80;
    shtTestHumidity = NAN;
    shtTestTemperature = 22;
    clockMs += 5000;
    sensor_manager_loop();
    sensor_manager_append_api(sensorObject);
    CHECK(std::fabs(sensorJson["battery_voltage"].as<double>() - 3.801) < 0.001);
    CHECK(sensorJson["battery_percentage"].as<int>() == 80);
    CHECK(sensorJson["temperature"].as<float>() == 22 && sensorJson["humidity"].as<float>() == 50);
    CHECK(shtTestLockHeld && Wire.address == 0x55 && Wire1.begins == 0);
    Wire.nackRegister = -1;
    Wire.shortRegister = 0x2c;
    Wire.registers[0x08] = 0xa0; Wire.registers[0x09] = 0x0f;
    clockMs += 2000;
    sensor_manager_loop();
    sensor_manager_append_api(sensorObject);
    CHECK(std::fabs(sensorJson["battery_voltage"].as<double>() - 4.0) < 0.001);
    CHECK(sensorJson["battery_percentage"].as<int>() == 80);
    Wire.shortRegister = -1;
    Wire.failedByteRegister = 0x09;
    Wire.registers[0x2c] = 101;
    clockMs += 2000;
    sensor_manager_loop();
    sensor_manager_append_api(sensorObject);
    CHECK(std::fabs(sensorJson["battery_voltage"].as<double>() - 4.0) < 0.001);
    CHECK(sensorJson["battery_percentage"].as<int>() == 80);
    Wire.failedByteRegister = -1;
    Wire.failByteWrite = true;
    clockMs += 2000;
    sensor_manager_loop();
    sensor_manager_append_api(sensorObject);
    CHECK(std::fabs(sensorJson["battery_voltage"].as<double>() - 4.0) < 0.001);
    Wire.failByteWrite = false;
    Wire.registers[0x08] = 0; Wire.registers[0x09] = 0;
    Wire.registers[0x2c] = 0;
    shtTestHumidity = 60;
    clockMs += 5000;
    sensor_manager_loop();
    sensor_manager_append_api(sensorObject);
    CHECK(std::fabs(sensorJson["battery_voltage"].as<double>() - 4.0) < 0.001);
    CHECK(sensorJson["battery_percentage"].as<int>() == 0 && sensorJson["humidity"].as<float>() == 60);
    Wire.registers[0x08] = 0x10; Wire.registers[0x09] = 0x0e;
    Wire.registers[0x2c] = 55;
    clockMs += 2000;
    sensor_manager_loop();
    sensor_manager_append_api(sensorObject);
    CHECK(std::fabs(sensorJson["battery_voltage"].as<double>() - 3.6) < 0.001);
    CHECK(sensorJson["battery_percentage"].as<int>() == 55 && !locked && !sticky_test_unlocked_wire0);
    CHECK(!sticky_test_invalid_gauge_transaction);
    for (const auto sample : {std::pair<float, float>{-6, 0}, {100.813f, 100},
                              {NAN, 100}, {INFINITY, 100}}) {
        shtTestHumidity = sample.first;
        clockMs += 5000;
        sensor_manager_loop();
        sensor_manager_append_api(sensorObject);
        CHECK(sensorJson["humidity"].as<float>() == sample.second);
    }
    shtTestError = 1;
    shtTestHumidity = 30;
    clockMs += 5000;
    sensor_manager_loop();
    sensor_manager_append_api(sensorObject);
    CHECK(sensorJson["humidity"].as<float>() == 100);
    shtTestError = 0;
    clockMs += 5000;
    sensor_manager_loop();
    sensor_manager_append_api(sensorObject);
    CHECK(sensorJson["humidity"].as<float>() == 30);
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
    for (uint8_t rotation = 0; rotation < 4; ++rotation) {
        touch.setRotation(rotation);
        for (auto point : {std::pair<uint16_t, uint16_t>{0, 0}, {799, 0}, {0, 479}, {799, 479}, {400, 240}}) {
            contact(point.second, point.first);
            auto sample = touch.readSample();
            CHECK(sample.status == TouchReadStatus::Fresh && sample.pressed);
            uint16_t nativeX = 0, nativeY = 0;
            CHECK(presented_frame_native_coords(800, 480, rotation,
                sample.horizontal, sample.vertical, &nativeX, &nativeY));
            CHECK(nativeX == point.first && nativeY == point.second);
        }
    }
    touch.setRotation(1);
    for (auto point : {std::pair<uint16_t, uint16_t>{17, 18}, {454, 32}, {463, 763}, {14, 777}}) {
        contact(point.first, 799 - point.second);
        auto sample = touch.readSample();
        CHECK(sample.horizontal == point.first && sample.vertical == point.second);
    }
    touch.setRotation(0);
    contact(479, 799);
    auto sample = touch.readSample();
    CHECK(sample.horizontal == 799 && sample.vertical == 479);
    contact(480, 799);
    CHECK(touch.readSample().status == TouchReadStatus::Error);
    CHECK(Wire1.registers[0x814e] == 0);
    if (Wire1.registers[0x814e] == 0) Wire1.registers[0x814e] = 0x80;
    sample = touch.readSample();
    CHECK(sample.status == TouchReadStatus::Fresh && !sample.pressed);
    contact(479, 799);
    sample = touch.readSample();
    CHECK(sample.status == TouchReadStatus::Fresh && sample.horizontal == 799 && sample.vertical == 479);
    resolution(123, 456);
    touch.init();
    CHECK(touch.readSample().status == TouchReadStatus::Error);
    Wire1.failRead = true;
    touch.init();
    CHECK(touch.readSample().status == TouchReadStatus::Error && Wire1.begins == 1);
    std::printf("Sticky board/touch checks: %d failures\n", failures);
    return failures ? 1 : 0;
}
