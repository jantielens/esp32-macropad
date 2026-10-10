#include "stopwatch_hardware.h"
#include "audio_output_driver.h"

#include "i2c_bus.h"
#include "log_manager.h"
#include <Arduino.h>
#include <Wire.h>
#include "drivers/arduino_gfx_co5300_driver.h"
#include "drivers/wire_cst820b_touch_driver.h"

namespace {
constexpr uint8_t kPmic = 0x6e;
uint8_t expander = 0;
bool wireStarted = false;
bool ready = false;
bool audioPowered = false;

// All helpers run with the shared Wire lock held.
bool probe(uint8_t address, bool reportFailure = true) {
    Wire.beginTransmission(address);
    const uint8_t status = Wire.endTransmission();
    if (status != 0 && !ready && reportFailure) {
        LOGW("StopWatch", "I2C probe failed: address=0x%02x status=%u", address, status);
    }
    return status == 0;
}

bool read(uint8_t address, uint8_t reg, uint8_t* data, size_t length) {
    Wire.beginTransmission(address);
    const bool written = Wire.write(reg) == 1;
    const uint8_t status = Wire.endTransmission(false);
    if (status != 0 || !written) {
        if (!ready) LOGE("StopWatch", "I2C read failed: address=0x%02x reg=0x%02x status=%u written=%u", address, reg, status, written);
        return false;
    }
    if (Wire.requestFrom(address, static_cast<uint8_t>(length)) != length ||
        Wire.available() < static_cast<int>(length)) {
        if (!ready) LOGE("StopWatch", "I2C short read: address=0x%02x reg=0x%02x length=%u", address, reg, static_cast<unsigned>(length));
        return false;
    }
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
    const uint8_t status = Wire.endTransmission();
    if ((status != 0 || !regWritten || !valueWritten) && !ready) {
        LOGE("StopWatch", "I2C write failed: address=0x%02x reg=0x%02x status=%u", address, reg, status);
    }
    return status == 0 && regWritten && valueWritten;
}

bool update(uint8_t address, uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t old;
    return read(address, reg, &old, 1) &&
        write(address, reg, (old & ~mask) | (value & mask));
}

void diagnose_expander() {
    for (uint8_t address : {uint8_t(0x4f), uint8_t(0x6f)}) {
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            const uint32_t frequency = attempt == 2 ? 400000 : 100000;
            if (!Wire.setClock(frequency)) {
                LOGE("StopWatch", "Diagnostic clock change failed: hz=%lu",
                     static_cast<unsigned long>(frequency));
                break;
            }
            if (attempt == 1) delay(800);
            const bool addressAck = probe(address, false);
            Wire.beginTransmission(address);
            Wire.write(0x00);
            Wire.endTransmission();
            delay(10);
            uint8_t identity[2] = {};
            const bool identityRead = read(address, 0x00, identity, sizeof(identity));
            if (!identityRead) memset(identity, 0xff, sizeof(identity));
            LOGW("StopWatch", "Expander diagnostic: address=0x%02x attempt=%u hz=%lu ack=%u uid_read=%u uid=%02x%02x",
                 address, attempt + 1, static_cast<unsigned long>(frequency),
                 addressAck, identityRead, identity[0], identity[1]);
            (void)addressAck;
        }
    }
    if (!Wire.setClock(100000)) {
        LOGE("StopWatch", "Failed to restore shared I2C clock to 100000 Hz");
    }
}

bool initialize() {
    if (!wireStarted) {
        if (!Wire.begin(47, 48, 100000)) {
            LOGE("StopWatch", "Wire initialization failed: SDA=47 SCL=48");
            return false;
        }
        wireStarted = true;
    }
    // The PMIC is always powered; its LDO/DCDC must be enabled before
    // discovering the expander on a cold battery boot.
    if (!probe(kPmic) ||
        !write(kPmic, 0x09, 0) ||
        !write(kPmic, 0x0a, 0) ||
        !update(kPmic, 0x06, 0x1f, 0x17)) return false;
    expander = 0;
    constexpr unsigned expanderTimeoutMs = 2000;
    constexpr unsigned expanderProbeIntervalMs = 10;
    for (unsigned waitedMs = expanderProbeIntervalMs; waitedMs <= expanderTimeoutMs;
         waitedMs += expanderProbeIntervalMs) {
        delay(expanderProbeIntervalMs);
        if (probe(0x4f, false)) expander = 0x4f;
        else if (probe(0x6f, false)) expander = 0x6f;
        if (expander) {
            LOGI("StopWatch", "Expander ready: address=0x%02x wait=%ums",
                  expander, waitedMs);
            break;
        }
    }
    if (!expander) {
        diagnose_expander();
        uint8_t identity[4] = {}, source = 0, power = 0;
           if (!read(kPmic, 0x00, identity, sizeof(identity))) memset(identity, 0xff, sizeof(identity));
           if (!read(kPmic, 0x04, &source, 1)) source = 0xff;
           if (!read(kPmic, 0x06, &power, 1)) power = 0xff;
           LOGE("StopWatch", "Expander unavailable after %ums; PMIC id=%02x model=%02x hw=%02x sw=%02x source=%02x power=%02x (ff=read failed)",
               expanderTimeoutMs, identity[0], identity[1], identity[2], identity[3], source, power);
        probe(0x4f);
        probe(0x6f);
        return false;
    }

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
        !update(expander, 0x05, 0x04, 0)) return false;

    // Preserve charging-current programming and power-button configuration.
    if (!update(kPmic, 0x16, 0x30, 0) ||
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

bool board_hardware_init() { return m5stack_stopwatch_init(); }

bool m5stack_stopwatch_ready() {
    if (!i2c_bus_lock()) return false;
    const bool result = ready;
    i2c_bus_unlock();
    return result;
}

static void initialize_stopwatch_panel(Arduino_DataBus* bus) {
    bus->sendCommand(0x11);
    delay(150);
    bus->beginWrite();
    bus->writeC8D8(0xc4, 0x80);
    bus->writeC8D8(0x35, 0x80);
    bus->writeC8D16(0x44, 0x01d2);
    bus->writeC8D8(0x3a, 0x55);
    bus->writeC8D8(0x53, 0x20);
    bus->writeCommand(0x20);
    bus->writeC8D8(0x36, 0);
    bus->writeC8D8(0x51, 0);
    bus->writeCommand(0x29);
    bus->endWrite();
}

CO5300PanelConfig board_co5300_config() {
    CO5300PanelConfig config;
    config.nativeWidth = 468;
    config.nativeHeight = 466;
    config.offsetX = 6;
    config.cropX = 1;
    config.tearingPin = 38;
    config.prepare = m5stack_stopwatch_ready;
    config.initialize = initialize_stopwatch_panel;
    return config;
}

CST820BConfig board_cst820b_config() {
    CST820BConfig config;
    config.nativeWidth = 468;
    config.nativeHeight = 466;
    config.cropX = 1;
    config.prepare = m5stack_stopwatch_ready;
    return config;
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

AudioBoardControl stopwatch_audio_control() {
    AudioBoardControl control;
    control.power = m5stack_stopwatch_audio_power;
    control.mute = m5stack_stopwatch_audio_mute;
    return control;
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