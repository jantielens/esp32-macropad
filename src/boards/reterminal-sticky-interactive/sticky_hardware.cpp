#include "sticky_hardware.h"
#include "board_config.h"
#include "action_continuation.h"
#include "audio.h"
#include "i2c_bus.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "device_dialog.h"
#include "display_manager.h"
#include "screen_saver_manager.h"
#if HAS_VOICE_NOTES
#include "voice_notes.h"
#endif
#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <atomic>
#include <string.h>
#if USE_SD_STORAGE
#include <SPI.h>
#endif

namespace {
esp_err_t powerError = ESP_ERR_INVALID_STATE;
bool ready = false;
bool sensorBusStarted = false;
#if USE_SD_STORAGE
SPIClass sharedSpi{HSPI};
#endif
std::atomic<uint32_t> shutdownToken{0};
std::atomic<bool> shutdownReserved{false};
bool shutdownGateOwned = false;
char shutdownMessage[CONFIG_DIALOG_MESSAGE_MAX_LEN] = {};
uint32_t shutdownDelayMs = 0;
uint32_t shutdownNotice = 0;
bool shutdownPrepared = false;
uint32_t shutdownPreparedAt = 0;
uint32_t shutdownRequestedAt = 0;
uint32_t finalNoticeGeneration = 0;
uint32_t finalNoticeRequest = 0;
bool shutdownStarted = false;
bool shutdownSent = false;
uint32_t shutdownStartedAt = 0;
uint32_t shutdownSentAt = 0;
constexpr uint32_t kButtonReleaseTimeoutMs = 5000;
constexpr uint32_t kPowerOffTimeoutMs = 500;
#if HAS_VOICE_NOTES
bool recorderWaitStarted = false;
uint32_t recorderWaitAt = 0;
constexpr uint32_t kRecorderStopTimeoutMs = 5000;
#endif

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
#if HAS_VOICE_NOTES
    recorderWaitStarted = false;
#endif
    shutdownToken.store(0);
    sticky_cancel_shutdown_notice(shutdownNotice);
    if (shutdownGateOwned) {
        ota_activity_finish();
        shutdownGateOwned = false;
    }
    if (shutdownNotice) {
        device_dialog_dismiss_notification(shutdownNotice);
        shutdownNotice = 0;
    }
    if (!action_continuation_complete(token, false))
        LOGE("Sticky", "Shutdown continuation expired: %lu", static_cast<unsigned long>(token));
    shutdownReserved.store(false);
}
}

bool sticky_early_power_init() {
    powerError = output(45, 1);
    if (powerError == ESP_OK) powerError = output(46, 1);
    return powerError == ESP_OK;
}

bool sticky_hardware_ready() { return ready; }

#if USE_SD_STORAGE
SPIClass& sticky_shared_spi() { return sharedSpi; }

bool sticky_prepare_sd() {
    if (!ready) {
        LOGE("Sticky", "SD unavailable: board initialization failed");
        return false;
    }
    const int pins[] = {SD_SPI_CS_PIN, SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN};
    int failedPin = -1;
    esp_err_t result = ESP_OK;
    sharedSpi.beginTransaction(SPISettings(SD_SPI_FREQUENCY_HZ, MSBFIRST, SPI_MODE0));
    for (const int pin : pins) {
        result = gpio_set_pull_mode(static_cast<gpio_num_t>(pin), GPIO_PULLUP_ONLY);
        if (result != ESP_OK) {
            failedPin = pin;
            break;
        }
    }
    if (result == ESP_OK) {
        result = output(10, 1);
        if (result != ESP_OK) failedPin = 10;
    }
    sharedSpi.endTransaction();
    if (result != ESP_OK) {
        LOGE("Sticky", "SD GPIO%d setup failed: %d", failedPin, int(result));
        return false;
    }
    delay(100);
    return true;
}

void sticky_sd_mount_failed() {
    if (ready) sharedSpi.beginTransaction(SPISettings(SD_SPI_FREQUENCY_HZ, MSBFIRST, SPI_MODE0));
    const esp_err_t deselectResult = output(8, 1);
    const esp_err_t powerResult = output(10, 0);
    if (ready) sharedSpi.endTransaction();
    if (deselectResult != ESP_OK || powerResult != ESP_OK)
        LOGE("Sticky", "Could not disable SD after mount failure: CS=%d power=%d",
             int(deselectResult), int(powerResult));
}
#endif

DeviceNotificationState sticky_prepare_shutdown_notice(uint32_t generation) {
    bool locked = false;
    display_manager_lock_if_needed(&locked);
    DeviceNotificationState state = device_dialog_notification_state(generation);
    if (state == DEVICE_NOTIFICATION_PRESENTED) {
        if (finalNoticeGeneration && finalNoticeGeneration != generation) {
            LOGW("Sticky", "Another shutdown notification owns final presentation");
            state = DEVICE_NOTIFICATION_MISSING;
        } else if (!finalNoticeRequest) {
            finalNoticeRequest = displayManager ? displayManager->interactionChanged(0, true) : 0;
            if (finalNoticeRequest) {
                finalNoticeGeneration = generation;
                sticky_display_hold_for_shutdown(finalNoticeRequest);
                state = DEVICE_NOTIFICATION_PENDING;
            } else {
                LOGW("Sticky", "Shutdown full refresh unavailable");
                state = DEVICE_NOTIFICATION_MISSING;
            }
        } else if (!sticky_display_hold_for_shutdown(finalNoticeRequest)) {
            state = DEVICE_NOTIFICATION_PENDING;
        }
    }
    display_manager_unlock_if_needed(locked);
    return state;
}

void sticky_cancel_shutdown_notice(uint32_t generation) {
    if (!generation || finalNoticeGeneration != generation) return;
    sticky_display_release_shutdown();
    finalNoticeGeneration = finalNoticeRequest = 0;
}

bool sticky_request_shutdown(uint32_t continuation_token, const char* message, uint32_t delay_ms) {
    if (!continuation_token || !ready || !message ||
        strlen(message) >= sizeof(shutdownMessage) || delay_ms > STICKY_SHUTDOWN_MAX_DELAY_MS) {
        LOGW("Sticky", "Shutdown rejected: board, continuation, message, or delay invalid");
        return false;
    }
    if (digitalRead(9) == HIGH) {
        LOGW("Sticky", "Shutdown rejected: disconnect USB/external power first");
        return false;
    }
    if (shutdownReserved.exchange(true)) {
        LOGW("Sticky", "Shutdown rejected: another shutdown is pending");
        return false;
    }
    const bool immediate = !message[0] && !delay_ms;
    if (immediate ? !ota_activity_try_begin() : ota_activity_is_busy()) {
        shutdownReserved.store(false);
        LOGW("Sticky", "Shutdown rejected: OTA, storage write, or power transition active");
        return false;
    }
    shutdownGateOwned = immediate;
    memcpy(shutdownMessage, message, strlen(message) + 1);
    shutdownDelayMs = delay_ms;
    shutdownPrepared = false;
    shutdownRequestedAt = static_cast<uint32_t>(millis());
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
        if (shutdownMessage[0]) screen_saver_manager_wake();
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
    if (!shutdownGateOwned) {
        if (uint32_t(now - shutdownRequestedAt) >= ACTION_CONTINUATION_TIMEOUT_MS) {
            shutdown_failed(token, "shutdown preparation timed out", false);
            return;
        }
        if (ota_activity_is_busy()) {
            shutdown_failed(token, "OTA, storage write, or power transition started", false);
            return;
        }
        if (!shutdownPrepared) {
            if (shutdownMessage[0]) {
                if (!screen_saver_manager_input_ready()) return;
                if (!device_dialog_notify(shutdownMessage, 0, &shutdownNotice)) {
                    shutdown_failed(token, "shutdown notification unavailable", false);
                    return;
                }
            }
            shutdownPreparedAt = now;
            shutdownPrepared = true;
        }
        if (shutdownNotice) {
            const DeviceNotificationState state = device_dialog_notification_state(shutdownNotice);
            if (state == DEVICE_NOTIFICATION_MISSING) {
                shutdown_failed(token, "shutdown notification replaced or dismissed", false);
                return;
            }
            if (state != DEVICE_NOTIFICATION_PRESENTED) return;
        }
        if (uint32_t(now - shutdownPreparedAt) < shutdownDelayMs) return;
        if (shutdownNotice) {
            const DeviceNotificationState state = sticky_prepare_shutdown_notice(shutdownNotice);
            if (state == DEVICE_NOTIFICATION_MISSING) {
                shutdown_failed(token, "shutdown final presentation unavailable", false);
                return;
            }
            if (state != DEVICE_NOTIFICATION_PRESENTED) return;
        }
        if (!ota_activity_try_begin()) {
            shutdown_failed(token, "OTA, storage write, or power transition active", false);
            return;
        }
        shutdownGateOwned = true;
    }
    // The AI/OK button directly enables the supply while held, bypassing the latch.
    if (digitalRead(4) == LOW) {
        if (static_cast<uint32_t>(now - shutdownStartedAt) >= kButtonReleaseTimeoutMs)
            shutdown_failed(token, "AI/OK button is still held", false);
        return;
    }
#if HAS_VOICE_NOTES
    const VoiceNoteCancelStatus recorder = voice_notes_cancel();
    if (recorder == VoiceNoteCancelStatus::Error) {
        shutdown_failed(token, "voice recorder could not stop safely", false);
        return;
    }
    if (recorder == VoiceNoteCancelStatus::Pending) {
        if (!recorderWaitStarted) {
            recorderWaitStarted = true;
            recorderWaitAt = now;
        } else if (static_cast<uint32_t>(now - recorderWaitAt) >= kRecorderStopTimeoutMs) {
            shutdown_failed(token, "voice recorder shutdown timed out", false);
        }
        return;
    }
#endif
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
#if USE_SD_STORAGE
    if (!sharedSpi.begin(EPAPER_SPI_SCK_PIN, EPAPER_SPI_MISO_PIN, EPAPER_SPI_MOSI_PIN, EPAPER_CS_PIN)) {
        sharedSpi.end();
        LOGE("Sticky", "Shared display/SD SPI initialization failed");
        return false;
    }
#endif
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
