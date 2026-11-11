#include "panel.h"
#include "app/board_config.h"
#include "app/log_manager.h"
#include "app/ota_activity.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace sticky_epaper {
namespace {
constexpr int panelWidth = 800, panelHeight = 480;
constexpr uint32_t busyTimeoutMs = 15000;
const SPISettings writeSettings(10000000, MSBFIRST, SPI_MODE0);
const SPISettings readSettings(4000000, MSBFIRST, SPI_MODE0);

uint8_t grayAt(const uint8_t* frame, int x, int y, bool grayscale) {
    if (grayscale) {
        const uint8_t packed = frame[size_t(y) * (panelWidth / 2) + x / 2];
        return (x & 1 ? packed & 15 : packed >> 4) / 5;
    }
    return frame[size_t(y) * (panelWidth / 8) + x / 8] & (0x80 >> (x & 7)) ? 3 : 0;
}
}

void Panel::command(uint8_t code, const uint8_t* data, size_t length) {
    spi.beginTransaction(writeSettings);
    digitalWrite(EPAPER_DC_PIN, LOW);
    digitalWrite(EPAPER_CS_PIN, LOW);
    spi.transfer(code);
    digitalWrite(EPAPER_CS_PIN, HIGH);
    digitalWrite(EPAPER_DC_PIN, HIGH);
    if (length) {
        digitalWrite(EPAPER_CS_PIN, LOW);
        spi.writeBytes(data, length);
        digitalWrite(EPAPER_CS_PIN, HIGH);
    }
    spi.endTransaction();
}

uint8_t Panel::read() {
    spi.beginTransaction(readSettings);
    digitalWrite(EPAPER_CS_PIN, LOW);
    const uint8_t value = spi.transfer(0);
    digitalWrite(EPAPER_CS_PIN, HIGH);
    spi.endTransaction();
    return value;
}

void Panel::reset(uint32_t lowMs, uint32_t highMs) {
    digitalWrite(EPAPER_RESET_PIN, LOW);
    delay(lowMs);
    digitalWrite(EPAPER_RESET_PIN, HIGH);
    if (highMs) delay(highMs);
}

Result Panel::waitReady(const char* stage, bool deferForOta) {
    const int active = controller == Controller::SSD2677 ? LOW : HIGH;
    const uint32_t start = millis();
    while (digitalRead(EPAPER_BUSY_PIN) == active) {
        if (deferForOta && ota_activity_is_active()) return Result::Deferred;
        if (millis() - start >= busyTimeoutMs) {
            LOGE("Sticky", "BUSY timeout at %s (active=%d)", stage, active);
            return Result::Error;
        }
        vTaskDelay(pdMS_TO_TICKS(2) ? pdMS_TO_TICKS(2) : 1);
    }
    return deferForOta && ota_activity_is_active() ? Result::Deferred : Result::Success;
}

void Panel::restorePower() {
    if (!powerCut) return;
    digitalWrite(EPAPER_POWER_PIN, HIGH);
    delay(100);
    powerCut = false;
}

Result Panel::finishRefresh(Result result) {
    // Close only the owned drive session during OTA; never start another refresh.
    if (result != Result::Error &&
        waitReady("SSD2677 before power off", false) == Result::Success) {
        const uint8_t zero = 0;
        command(0x02, &zero, 1);
        if (waitReady("SSD2677 power off", false) == Result::Success) return result;
    }
    digitalWrite(EPAPER_POWER_PIN, LOW);
    powerCut = true;
    LOGE("Sticky", "Panel drive cleanup failed; power cut until the next full-refresh retry");
    delay(100);
    return Result::Error;
}

bool Panel::detectByBusy() {
    reset(20, 0);
    const int initial = digitalRead(EPAPER_BUSY_PIN);
    const uint32_t start = millis();
    while (millis() - start < 1500) {
        delay(2);
        const int level = digitalRead(EPAPER_BUSY_PIN);
        if (level == initial) continue;
        bool stable = true;
        for (unsigned sample = 0; sample < 10; ++sample) {
            delay(2);
            if (digitalRead(EPAPER_BUSY_PIN) != level) stable = false;
        }
        if (!stable) continue;
        controller = level == HIGH ? Controller::SSD2677 : Controller::SSD1677;
        LOGW("Sticky", "No driven SPI ID; BUSY transition selects %s (hardware verification required)",
             level == HIGH ? "SSD2677" : "SSD1677");
        return true;
    }
    LOGE("Sticky", "Panel presence unconfirmed: undriven SPI ID and no BUSY transition");
    return false;
}

bool Panel::begin() {
    pinMode(EPAPER_POWER_PIN, OUTPUT);
    restorePower();
    pinMode(EPAPER_CS_PIN, OUTPUT);
    digitalWrite(EPAPER_CS_PIN, HIGH);
    pinMode(EPAPER_DC_PIN, OUTPUT);
    pinMode(EPAPER_RESET_PIN, OUTPUT);
    pinMode(EPAPER_BUSY_PIN, INPUT);
    spi.begin(EPAPER_SPI_SCK_PIN, EPAPER_SPI_MISO_PIN, EPAPER_SPI_MOSI_PIN, EPAPER_CS_PIN);
    started = true;
    reset(20, 10);
    command(0x70);
    const uint8_t id = read();
    LOGI("Sticky", "Panel probe ID=0x%02x", id);
    if (id == 7) controller = Controller::SSD2677;
    else if (id != 0 && id != 0xff) controller = Controller::SSD1677;
    else if (!detectByBusy()) return false;
    Result result = initialize(false);
    if (result == Result::Error) {
        const Controller failed = controller;
        if (!detectByBusy() || controller == failed) {
            LOGE("Sticky", "No evidence for an alternate controller after initialization failure");
            return false;
        }
        LOGW("Sticky", "Selected controller failed initialization; retrying other controller");
        result = initialize(false);
    }
    if (result != Result::Success) return false;
    LOGI("Sticky", "%s initialized; physical rendering not yet verified",
         controller == Controller::SSD2677 ? "SSD2677" : "SSD1677");
    return true;
}

void Panel::end() {
    if (started) spi.end();
    started = false;
    controller = Controller::Unknown;
}

Result Panel::initialize(bool grayscale) {
    if (ota_activity_is_active()) return Result::Deferred;
    restorePower();
    if (controller == Controller::SSD1677) {
        reset(10, 10);
        command(0x12);
        Result result = waitReady("SSD1677 reset");
        if (result != Result::Success) return result;
        const uint8_t booster[] = {0xae, 0xc7, 0xc3, 0xc0, 0x80};
        const uint8_t output[] = {0xdf, 0x01, 0x02};
        const uint8_t entry = 0x03, temperature = 0x80;
        const uint8_t border = grayscale ? 0x00 : 0x05;
        const uint8_t xRange[] = {0, 0, 0x1f, 0x03}, yRange[] = {0, 0, 0xdf, 0x01};
        const uint8_t zero[] = {0, 0}, grayTemperature[] = {0x67, 0};
        command(0x0c, booster, sizeof(booster));
        command(0x3c, &border, 1);
        command(0x01, output, sizeof(output));
        command(0x11, &entry, 1);
        command(0x44, xRange, sizeof(xRange));
        command(0x45, yRange, sizeof(yRange));
        command(0x18, &temperature, 1);
        if (grayscale) command(0x1a, grayTemperature, sizeof(grayTemperature));
        command(0x4e, zero, sizeof(zero));
        command(0x4f, zero, sizeof(zero));
    } else {
        reset(20, 50);
        Result result = waitReady("SSD2677 reset");
        if (result != Result::Success) return result;
        const uint8_t panel[] = {0x2f, 0x0e}, booster[] = {0x0f, 0x8b, 0x93, 0xc1};
        const uint8_t timing[] = {0x76, 0x76, 0x76, 0x5a, 0x9d, 0x8a, 0x76, 0x62};
        const uint8_t resolution[] = {0x03, 0x20, 0x02, 0xa8}, zero[] = {0, 0, 0, 0};
        const uint8_t e7 = 0xc1, pll = 0x08, border = 0x77, e0 = 0x10, e9 = 1;
        command(0x00, panel, sizeof(panel));
        result = waitReady("SSD2677 PSR");
        if (result != Result::Success) return result;
        command(0x06, booster, sizeof(booster));
        command(0xe7, &e7, 1);
        command(0x30, &pll, 1);
        command(0x50, &border, 1);
        command(0x62, timing, sizeof(timing));
        command(0x61, resolution, sizeof(resolution));
        command(0xe0, &e0, 1);
        command(0x65, zero, sizeof(zero));
        command(0xe9, &e9, 1);
    }
    return waitReady("controller setup");
}

Result Panel::latchWaveform(bool grayscale, bool full) {
    command(0x40);
    Result result = waitReady("temperature");
    if (result != Result::Success) return result;
    uint8_t temperature = read();
    if (!temperature || temperature > 127) {
        if (!temperatureWarning) {
            LOGW("Sticky", "Panel temperature read=0x%02x unavailable; using vendor 25C waveform baseline", temperature);
            temperatureWarning = true;
        }
        temperature = 25;
    }
    const uint8_t bank = 0x12;
    const uint8_t waveform = grayscale ? (temperature <= 15 ? 0xf1 : 0xfa) :
        full ? (temperature <= 10 ? 0xe8 : temperature <= 20 ? 0xeb : temperature <= 30 ? 0xee : 0xf1) :
               (temperature <= 10 ? 0x08 : temperature <= 20 ? 0x0f : temperature <= 30 ? 0x19 : 0x23);
    command(0xe0, &bank, 1);
    command(0xe6, &waveform, 1);
    command(0xa5);
    result = waitReady("waveform latch");
    if (result == Result::Success) delay(10);
    return result;
}

Result Panel::stream(uint8_t code, const uint8_t* current, const uint8_t* previous,
                     bool grayscale, bool full, bool highPlane) {
    command(code);
    uint8_t row[panelWidth / 4];
    for (int y = 0; y < panelHeight; ++y) {
        if (ota_activity_is_active()) return Result::Deferred;
        const bool twoBits = controller == Controller::SSD2677;
        const int pixelsPerByte = twoBits ? 4 : 8;
        for (int column = 0; column < panelWidth / pixelsPerByte; ++column) {
            uint8_t packed = 0;
            for (int pixel = 0; pixel < pixelsPerByte; ++pixel) {
                const int outputX = column * pixelsPerByte + pixel;
                // Vendor Sticky mono flips native X once; its Gray4 path flips
                // twice (panel + controller). Keep that net transform here.
                const int x = grayscale ? outputX : panelWidth - 1 - outputX;
                const uint8_t gray = grayAt(current, x, y, grayscale);
                uint8_t value;
                if (twoBits) {
                    value = !grayscale && !full
                        ? uint8_t((grayAt(previous, x, y, false) ? 2 : 0) | (gray ? 1 : 0)) : gray;
                } else if (grayscale) {
                    value = highPlane ? !(gray & 1) : !(gray & 2);
                } else {
                    value = gray ? 1 : 0;
                }
                packed = uint8_t((packed << (twoBits ? 2 : 1)) | value);
            }
            row[column] = packed;
        }
        spi.beginTransaction(writeSettings);
        digitalWrite(EPAPER_CS_PIN, LOW);
        spi.writeBytes(row, panelWidth / pixelsPerByte);
        digitalWrite(EPAPER_CS_PIN, HIGH);
        spi.endTransaction();
        if ((y & 15) == 15) vTaskDelay(1);
    }
    return Result::Success;
}

Result Panel::refresh(const uint8_t* current, const uint8_t* previous, bool grayscale, bool full) {
    Result result = initialize(grayscale);
    if (result != Result::Success) return result;
    if (controller == Controller::SSD1677) {
        if (!full && !grayscale) {
            const uint8_t border = 0x80;
            command(0x3c, &border, 1);
        }
        if (grayscale) {
            result = stream(0x24, current, previous, true, true);
            if (result == Result::Success) result = stream(0x26, current, previous, true, true, true);
        } else {
            result = stream(0x26, previous, previous, false, true);
            if (result == Result::Success) result = stream(0x24, current, previous, false, true);
        }
        if (result != Result::Success) return result;
        if (ota_activity_is_active()) return Result::Deferred;
        const uint8_t mode = grayscale ? 0xd7 : full ? 0xf7 : 0xff;
        command(0x22, &mode, 1);
        command(0x20);
        return waitReady("SSD1677 refresh");
    }
    result = latchWaveform(grayscale, full);
    if (result == Result::Success) result = stream(0x10, current, previous, grayscale, full);
    if (result != Result::Success) return result;
    if (ota_activity_is_active()) return Result::Deferred;
    command(0x04);
    result = waitReady("SSD2677 power on");
    if (result == Result::Success) {
        const uint8_t zero = 0;
        command(0x12, &zero, 1);
        result = waitReady("SSD2677 refresh");
    }
    return finishRefresh(result);
}
}
