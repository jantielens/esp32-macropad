#include "m5stack_stopwatch.h"

#if HAS_M5STACK_STOPWATCH
#include "i2c_bus.h"
#include "log_manager.h"
#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr uint8_t kPmic = 0x6e;
constexpr uint8_t kTouch = 0x15;
uint8_t expander = 0;
bool wireStarted = false;
bool ready = false;
bool audioPowered = false;

// All helpers run with the shared Wire lock held.
bool probe(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
}

bool read(uint8_t address, uint8_t reg, uint8_t* data, size_t length) {
    Wire.beginTransmission(address);
    const bool written = Wire.write(reg) == 1;
    if (Wire.endTransmission(false) != 0 || !written) return false;
    if (Wire.requestFrom(address, static_cast<uint8_t>(length)) != length ||
        Wire.available() < static_cast<int>(length)) return false;
    for (size_t i = 0; i < length; ++i) {
        const int value = Wire.read();
        if (value < 0) return false;
        data[i] = static_cast<uint8_t>(value);
    }
    return true;
}

bool write(uint8_t address, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(address);
    const bool regWritten = Wire.write(reg) == 1;
    const bool valueWritten = Wire.write(value) == 1;
    return Wire.endTransmission() == 0 && regWritten && valueWritten;
}

bool update(uint8_t address, uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t old;
    return read(address, reg, &old, 1) &&
        write(address, reg, (old & ~mask) | (value & mask));
}

bool initialize() {
    if (!wireStarted) {
        if (!Wire.begin(47, 48, 100000)) return false;
        wireStarted = true;
    }
    if (probe(0x4f)) expander = 0x4f;
    else if (probe(0x6f)) expander = 0x6f;
    else return false;

    // Latch motor/PA low before making IO9/IO10 push-pull outputs.
    if (!write(expander, 0x23, 0) ||
        !update(expander, 0x06, 0x03, 0) ||
        // Retained PWM overrides the GPIO latch: PWM1=IO9, PWM2=IO8,
        // PWM4=IO10. Leave PWM3 (unowned IO11) and shared frequency alone.
        !update(expander, 0x1c, 0x80, 0) ||
        !update(expander, 0x1e, 0x80, 0) ||
        !update(expander, 0x22, 0x80, 0) ||
        !update(expander, 0x14, 0x03, 0) ||
        !update(expander, 0x04, 0x03, 0x03) ||
        !update(expander, 0x05, 0x04, 0) ||
        !probe(kPmic)) return false;

    // Preserve charging-current programming and power-button configuration.
    if (!write(kPmic, 0x09, 0) ||
        !write(kPmic, 0x0a, 0) ||
        !update(kPmic, 0x06, 0x1f, 0x17) ||
        !update(kPmic, 0x16, 0x30, 0) ||
        !update(kPmic, 0x10, 0x04, 0) ||
        !update(kPmic, 0x14, 0x30, 0)) return false;

    // IO1 USB mux HIGH, IO3 audio OFF, IO4/IO5 resets, IO8 OLED power.
    if (!update(expander, 0x13, 0x9d, 0) ||
        !update(expander, 0x03, 0x9d, 0x9d) ||
        !update(expander, 0x05, 0x9d, 0x99)) return false;
    delay(10);
    if (!update(expander, 0x05, 0x18, 0)) return false;
    delay(10);
    if (!update(expander, 0x05, 0x18, 0x18)) return false;
    delay(50);
    return true;
}
} // namespace

bool m5stack_stopwatch_init() {
    i2c_bus_init();
    if (!i2c_bus_lock()) return false;
    if (!ready) {
        audioPowered = false;
        ready = initialize();
        if (!ready && expander) {
            // Best-effort safe outputs even if the bus failed midway.
            update(expander, 0x06, 0x03, 0);
            update(expander, 0x05, 0x9c, 0);
            update(expander, 0x1c, 0x80, 0);
            update(expander, 0x1e, 0x80, 0);
            update(expander, 0x22, 0x80, 0);
        }
    }
    const bool result = ready;
    i2c_bus_unlock();
    return result;
}

bool m5stack_stopwatch_ready() {
    if (!i2c_bus_lock()) return false;
    const bool result = ready;
    i2c_bus_unlock();
    return result;
}

bool m5stack_stopwatch_audio_power(bool enabled) {
    if (!i2c_bus_lock()) return false;
    const bool muted = ready && update(expander, 0x06, 0x02, 0);
    // A mute transport failure must not prevent attempting to shut the rail off.
    const bool powered = ready && (muted || !enabled) &&
        update(expander, 0x05, 0x04, enabled ? 0x04 : 0);
    const bool ok = muted && powered;
    audioPowered = ok && enabled;
    i2c_bus_unlock();
    if (ok && enabled) delay(10);
    return ok;
}

bool m5stack_stopwatch_audio_mute(bool muted) {
    if (!i2c_bus_lock()) return false;
    const bool ok = ready && (muted || audioPowered) &&
        update(expander, 0x06, 0x02, muted ? 0 : 0x02);
    i2c_bus_unlock();
    return ok;
}

bool m5stack_stopwatch_touch_read(uint8_t* data, size_t length) {
    if (!data || length != 5 || !i2c_bus_lock()) return false;
    const bool ok = ready && read(kTouch, 0x02, data, length);
    i2c_bus_unlock();
    return ok;
}

bool m5stack_stopwatch_battery_read(uint16_t& millivolts, bool& charging) {
    if (!i2c_bus_lock()) return false;
    uint8_t voltages[4], gpio;
    const bool ok = ready && read(kPmic, 0x22, voltages, sizeof(voltages)) &&
        read(kPmic, 0x12, &gpio, 1);
    if (ok) {
        millivolts = voltages[0] | (uint16_t(voltages[1]) << 8);
        const uint16_t vin = voltages[2] | (uint16_t(voltages[3]) << 8);
        charging = vin > 4000 && !(gpio & 0x04);
    }
    i2c_bus_unlock();
    return ok;
}
#endif
