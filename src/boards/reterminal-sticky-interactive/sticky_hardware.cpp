#include "sticky_hardware.h"
#include "board_config.h"
#include "action_continuation.h"
#include "audio.h"
#include "i2c_bus.h"
#include "log_manager.h"
#include "ota_activity.h"
#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <atomic>

namespace {
esp_err_t powerError = ESP_ERR_INVALID_STATE;
bool ready = false;
bool sensorBusStarted = false;
std::atomic<uint32_t> shutdownToken{0};
bool shutdownStarted = false;
bool shutdownSent = false;
uint32_t shutdownStartedAt = 0;
uint32_t shutdownSentAt = 0;
constexpr uint32_t kButtonReleaseTimeoutMs = 5000;
constexpr uint32_t kPowerOffTimeoutMs = 500;

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

void shutdown_failed(uint32_t token, const char* reason, bool restoreLatch) {
    if (restoreLatch) {
        const bool clockLow = checked_output(46, 0);
        const bool holdHigh = checked_output(45, 1);
        const bool clockHigh = holdHigh && checked_output(46, 1);
        if (!clockLow || !holdHigh || !clockHigh)
            LOGE("Sticky", "Power latch restoration failed; hardware recovery may be required");
    }
    LOGE("Sticky", "Shutdown failed: %s", reason);
    shutdownStarted = false;
    shutdownSent = false;
    shutdownToken.store(0);
    ota_activity_finish();
    if (!action_continuation_complete(token, false))
        LOGE("Sticky", "Shutdown continuation expired: %lu", static_cast<unsigned long>(token));
}
}

bool sticky_early_power_init() {
    powerError = output(45, 1);
    if (powerError == ESP_OK) powerError = output(46, 1);
    return powerError == ESP_OK;
}

bool sticky_hardware_ready() { return ready; }

bool sticky_request_shutdown(uint32_t continuation_token) {
    if (!continuation_token || !ready) {
        LOGW("Sticky", "Shutdown rejected: board not ready or no action continuation");
        return false;
    }
    if (!ota_activity_try_begin()) {
        LOGW("Sticky", "Shutdown rejected: OTA, config write, or power transition active");
        return false;
    }
    shutdownToken.store(continuation_token);
    return true;
}

void sticky_hardware_loop() {
    const uint32_t token = shutdownToken.load();
    if (!token) return;
    const uint32_t now = static_cast<uint32_t>(millis());
    if (!shutdownStarted) {
        shutdownStarted = true;
        shutdownStartedAt = now;
    }
    if (shutdownSent) {
        if (static_cast<uint32_t>(now - shutdownSentAt) >= kPowerOffTimeoutMs)
            shutdown_failed(token, "device remains powered after latch release", true);
        return;
    }
    if (digitalRead(9) == HIGH) {
        shutdown_failed(token, "disconnect USB/external power first", false);
        return;
    }
    // The AI/OK button directly enables the supply while held, bypassing the latch.
    if (digitalRead(4) == LOW) {
        if (static_cast<uint32_t>(now - shutdownStartedAt) >= kButtonReleaseTimeoutMs)
            shutdown_failed(token, "AI/OK button is still held", false);
        return;
    }
#if HAS_AUTO_SHUTDOWN
    if (sticky_display_is_busy()) return;
#endif
#if HAS_AUDIO
    audio_stop();
#endif
    // Rev 01 U3 (74AHC1G79): Q samples HOLD on LOCK's rising edge.
    if (!checked_output(46, 0) || !checked_output(45, 0) || !checked_output(46, 1)) {
        shutdown_failed(token, "power latch GPIO write failed", true);
        return;
    }
    shutdownSentAt = static_cast<uint32_t>(millis());
    shutdownSent = true;
}

bool board_hardware_init() {
    if (ready) return true;
    if (powerError != ESP_OK) {
        LOGE("Sticky", "Early power latch failed: %d", int(powerError));
        return false;
    }
    // Keep deferred peripherals off and the buzzer low before audio initializes.
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
