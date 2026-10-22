#include "m5stack_core2_hardware.h"

#include "action_continuation.h"
#include "audio.h"
#include "i2c_bus.h"
#include "log_manager.h"
#include "ota_activity.h"

#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr uint8_t kAxp192Address = 0x34;
constexpr uint32_t kShutdownTimeoutMs = 500;
bool ready = false;
uint32_t shutdownToken = 0;
uint32_t shutdownDeadline = 0;
bool shutdownCommandSent = false;

bool read_register(uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(kAxp192Address);
    const bool written = Wire.write(reg) == 1;
    if (!written || Wire.endTransmission(false) != 0 ||
        Wire.requestFrom(kAxp192Address, uint8_t(1)) != 1 || Wire.available() < 1) return false;
    const int result = Wire.read();
    if (result < 0) return false;
    value = static_cast<uint8_t>(result);
    return true;
}

bool write_register(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(kAxp192Address);
    const bool registerWritten = Wire.write(reg) == 1;
    const bool valueWritten = Wire.write(value) == 1;
    return registerWritten && valueWritten && Wire.endTransmission() == 0;
}

bool update_register(uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t current = 0;
    return read_register(reg, current) && write_register(reg, (current & ~mask) | (value & mask));
}

bool set_dcdc_voltage(uint8_t reg, uint16_t millivolts) {
    if (millivolts < 700 || millivolts > 3500) return false;
    uint8_t current = 0;
    const uint8_t setting = static_cast<uint8_t>((millivolts - 700) / 25) & 0x7f;
    return read_register(reg, current) && write_register(reg, (current & 0x80) | setting);
}

bool set_core2_backlight(uint8_t brightness) {
    if (brightness > 100) brightness = 100;
    if (!brightness) return update_register(0x12, 0x02, 0);
    const uint16_t millivolts = 2500 + (uint32_t(800) * (brightness - 1)) / 99;
    return set_dcdc_voltage(0x27, millivolts) && update_register(0x12, 0x02, 0x02);
}

bool initialize_pmic() {
    if (!Wire.begin(TOUCH_I2C_SDA, TOUCH_I2C_SCL, 400000)) return false;
    delay(10);

    Wire.beginTransmission(kAxp192Address);
    if (Wire.endTransmission() != 0) return false;

    uint8_t value = 0;
    if (!read_register(0x30, value) || !write_register(0x30, (value & 0x04) | 0x02) ||
        !update_register(0x92, 0x07, 0) || !update_register(0x93, 0x07, 0) ||
        !read_register(0x35, value) || !write_register(0x35, (value & 0x1c) | 0xa2) ||
        !set_dcdc_voltage(0x26, 3350) || !set_dcdc_voltage(0x27, 2800)) return false;

    if (!read_register(0x28, value) || !write_register(0x28, (value & 0x0f) | 0xf0) ||
        !read_register(0x28, value) || !write_register(0x28, (value & 0xf0) | 0x02) ||
        !update_register(0x12, 0x06, 0x06) ||
        !update_register(0x94, 0x02, 0) ||
        !read_register(0x95, value) || !write_register(0x95, (value & 0x72) | 0x84) ||
        !write_register(0x36, 0x4c) || !write_register(0x82, 0xff) ||
        !update_register(0x96, 0x02, 0)) return false;
    delay(100);
    if (!update_register(0x96, 0x02, 0x02)) return false;
    delay(100);

    uint8_t powerStatus = 0;
    if (!read_register(0x00, powerStatus)) return false;
    if (powerStatus & 0x08) {
        if (!read_register(0x30, value) || !write_register(0x30, value | 0x80) ||
            !read_register(0x91, value) || !write_register(0x91, (value & 0x0f) | 0xf0) ||
            !read_register(0x90, value) || !write_register(0x90, (value & 0xf8) | 0x07) ||
            !read_register(0x10, value) || !write_register(0x10, value & ~0x04)) return false;
    } else {
        if (!read_register(0x91, value) || !write_register(0x91, (value & 0x0f) | 0xf0) ||
            !read_register(0x90, value) || !write_register(0x90, (value & 0xf8) | 0x02) ||
            !read_register(0x10, value) || !write_register(0x10, value | 0x04)) return false;
    }
    return set_core2_backlight(100);
}

bool set_speaker_locked(bool enabled) {
    return update_register(0x94, 0x04, enabled ? 0x04 : 0);
}
}

bool board_hardware_init() {
    i2c_bus_init();
    if (!i2c_bus_lock()) return false;
    if (!ready) ready = initialize_pmic();
    i2c_bus_unlock();
    if (!ready) LOGE("Core2", "AXP192 initialization failed");
    else LOGI("Core2", "AXP192 power rails initialized");
    return ready;
}

bool m5stack_core2_set_backlight_brightness(uint8_t brightness) {
    if (!i2c_bus_lock()) return false;
    const bool ok = ready && set_core2_backlight(brightness);
    i2c_bus_unlock();
    return ok;
}

bool m5stack_core2_set_speaker(bool enabled) {
    if (!i2c_bus_lock()) return false;
    const bool ok = ready && set_speaker_locked(enabled);
    i2c_bus_unlock();
    return ok;
}

bool m5stack_core2_battery_read(uint16_t& millivolts, bool& charging) {
    if (!i2c_bus_lock()) return false;
    uint8_t voltage[2] = {};
    uint8_t status = 0;
    const bool ok = ready && read_register(0x78, voltage[0]) &&
        read_register(0x79, voltage[1]) && read_register(0x00, status);
    if (ok) {
        millivolts = static_cast<uint16_t>(((voltage[0] << 4) | (voltage[1] & 0x0f)) * 11 / 10);
        charging = (status & 0x04) != 0;
    }
    i2c_bus_unlock();
    return ok;
}

bool m5stack_core2_request_shutdown(uint32_t continuation_token) {
    if (!continuation_token || !ota_activity_try_begin()) return false;
    if (!i2c_bus_lock()) {
        ota_activity_finish();
        return false;
    }
    const bool accepted = ready && !shutdownToken;
    if (accepted) {
        shutdownToken = continuation_token;
        shutdownCommandSent = false;
    }
    i2c_bus_unlock();
    if (!accepted) ota_activity_finish();
    return accepted;
}

void m5stack_core2_loop() {
    if (!i2c_bus_lock()) return;
    const uint32_t token = shutdownToken;
    const bool sent = shutdownCommandSent;
    const bool timedOut = sent && static_cast<int32_t>(millis() - shutdownDeadline) >= 0;
    if (timedOut) shutdownToken = 0;
    i2c_bus_unlock();
    if (!token || (sent && !timedOut)) return;

    bool failed = timedOut;
    if (!sent) {
        audio_stop();
        if (!i2c_bus_lock()) return;
        uint8_t command = 0;
        const bool accepted = ready && shutdownToken == token &&
            read_register(0x32, command) && write_register(0x32, command | 0x80);
        if (accepted) {
            shutdownCommandSent = true;
            shutdownDeadline = millis() + kShutdownTimeoutMs;
        } else {
            shutdownToken = 0;
            failed = true;
        }
        i2c_bus_unlock();
    }
    if (failed) {
        ota_activity_finish();
        action_continuation_complete(token, false);
        LOGE("Core2", "Shutdown command failed; device remains powered");
    }
}

AudioBoardControl m5stack_core2_audio_control() {
    AudioBoardControl control;
    control.mute = [](bool muted) { return m5stack_core2_set_speaker(!muted); };
    return control;
}