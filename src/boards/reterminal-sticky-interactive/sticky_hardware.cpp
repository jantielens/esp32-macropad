#include "sticky_hardware.h"
#include "board_config.h"
#include "i2c_bus.h"
#include "log_manager.h"
#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>

namespace {
esp_err_t powerError = ESP_ERR_INVALID_STATE;
bool ready = false;
bool sensorBusStarted = false;

esp_err_t output(int pin, int level) {
    const auto gpio = static_cast<gpio_num_t>(pin);
    // Preload the latch before enabling output, especially the SD CS and power clock.
    esp_err_t result = gpio_set_level(gpio, level);
    if (result == ESP_OK) result = gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    return result;
}

bool checked_output(int pin, int level) {
    const esp_err_t result = output(pin, level);
    if (result != ESP_OK) LOGE("Sticky", "GPIO%d output=%d failed: %d", pin, level, int(result));
    return result == ESP_OK;
}
}

bool sticky_early_power_init() {
    powerError = output(45, 1);
    if (powerError == ESP_OK) powerError = output(46, 1);
    return powerError == ESP_OK;
}

bool sticky_hardware_ready() { return ready; }

bool board_hardware_init() {
    if (ready) return true;
    if (powerError != ESP_OK) {
        LOGE("Sticky", "Early power latch failed: %d", int(powerError));
        return false;
    }
    // No SD, microphone, or buzzer backend takes ownership in this build.
    if (!checked_output(8, 1) || !checked_output(10, 0) ||
        !checked_output(38, 0) || !checked_output(48, 0) ||
        !checked_output(39, 0) || !checked_output(EPAPER_CS_PIN, 1)) return false;
    const esp_err_t inputResult = gpio_set_direction(GPIO_NUM_9, GPIO_MODE_INPUT);
    if (inputResult != ESP_OK) {
        LOGE("Sticky", "External-power input configuration failed: %d", int(inputResult));
        return false;
    }
    i2c_bus_init();
    if (!i2c_bus_lock()) {
        LOGE("Sticky", "Sensor bus lock unavailable");
        return false;
    }
    if (!sensorBusStarted) sensorBusStarted = Wire.begin(SENSOR_I2C_SDA, SENSOR_I2C_SCL, SENSOR_I2C_FREQUENCY);
    i2c_bus_unlock();
    if (!sensorBusStarted) {
        LOGE("Sticky", "Wire0 initialization failed: SDA=%d SCL=%d", SENSOR_I2C_SDA, SENSOR_I2C_SCL);
        return false;
    }
    if (!checked_output(42, 1)) return false;
    delay(250);
    if (!checked_output(EPAPER_POWER_PIN, 1)) return false;
    delay(100);
    ready = true;
    LOGI("Sticky", "Board ready; Wire0 sensors SDA1/SCL0, Wire1 reserved for GT911 SDA3/SCL2");
    return true;
}
