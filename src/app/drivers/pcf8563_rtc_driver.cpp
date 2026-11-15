#include "rtc_driver.h"
#include "board_config.h"
#include "i2c_bus.h"
#include "log_manager.h"
#include <Wire.h>
#include <stdint.h>

namespace {
constexpr uint8_t kControlRegister = 0x00;
constexpr uint8_t kTimeRegister = 0x02;
constexpr uint8_t kStop = 0x20;
constexpr uint8_t kVoltageLow = 0x80;
constexpr uint8_t kCentury = 0x80;
constexpr time_t kEpoch2000 = 946684800;
constexpr uint8_t kMonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

class BusLock {
public:
    BusLock() : acquired(i2c_bus_lock()) {
        if (!acquired) LOGE("RTC", "Wire0 lock unavailable");
    }
    ~BusLock() { if (acquired) i2c_bus_unlock(); }
    bool acquired;
};

bool read_registers(uint8_t reg, uint8_t* output, uint8_t count) {
    Wire.beginTransmission(RTC_I2C_ADDR);
    const bool queued = Wire.write(reg) == 1;
    const uint8_t result = Wire.endTransmission(false);
    if (!queued || result != 0) {
        LOGE("RTC", "PCF8563 register selection failed: 0x%02x", reg);
        return false;
    }
    if (Wire.requestFrom(uint8_t(RTC_I2C_ADDR), count) != count) {
        LOGE("RTC", "PCF8563 short read: 0x%02x", reg);
        return false;
    }
    for (uint8_t index = 0; index < count; ++index) {
        const int value = Wire.read();
        if (value < 0) {
            LOGE("RTC", "PCF8563 byte read failed: 0x%02x", reg + index);
            return false;
        }
        output[index] = uint8_t(value);
    }
    return true;
}

bool write_registers(uint8_t reg, const uint8_t* values, uint8_t count) {
    Wire.beginTransmission(RTC_I2C_ADDR);
    bool queued = Wire.write(reg) == 1;
    for (uint8_t index = 0; index < count && queued; ++index)
        queued = Wire.write(values[index]) == 1;
    const uint8_t result = Wire.endTransmission();
    if (!queued || result != 0) {
        LOGE("RTC", "PCF8563 write failed: 0x%02x (status=%u)", reg, unsigned(result));
        return false;
    }
    return true;
}

bool decode_bcd(uint8_t value, unsigned minimum, unsigned maximum, unsigned& output) {
    if ((value & 15) > 9 || (value >> 4) > 9) return false;
    output = (value >> 4) * 10 + (value & 15);
    return output >= minimum && output <= maximum;
}

uint8_t encode_bcd(unsigned value) { return uint8_t((value / 10) * 16 + value % 10); }

class Pcf8563RtcDriver final : public RtcDriver {
public:
    bool begin() override {
        BusLock lock;
        uint8_t control;
        return lock.acquired && read_registers(kControlRegister, &control, 1);
    }

    RtcReadResult readUtc(time_t& epoch) override {
        BusLock lock;
        uint8_t data[9];
        if (!lock.acquired || !read_registers(kControlRegister, data, sizeof(data)))
            return RtcReadResult::IoError;
        // STOP/test modes and voltage loss cannot establish trusted time.
        // This driver uses century=0 for 2000-2099, century=1 for 1900-1999.
        if ((data[0] & (kStop | 0x88)) || (data[2] & kVoltageLow) || (data[7] & kCentury))
            return RtcReadResult::InvalidTime;
        unsigned second, minute, hour, day, month, year;
        if (!decode_bcd(data[2] & 0x7f, 0, 59, second) ||
            !decode_bcd(data[3] & 0x7f, 0, 59, minute) ||
            !decode_bcd(data[4] & 0x3f, 0, 23, hour) ||
            !decode_bcd(data[5] & 0x3f, 1, 31, day) ||
            (data[6] & 7) > 6 ||
            !decode_bcd(data[7] & 0x1f, 1, 12, month) ||
            !decode_bcd(data[8], 0, 99, year))
            return RtcReadResult::InvalidTime;
        const unsigned maximum_day = kMonthDays[month - 1] + (month == 2 && year % 4 == 0);
        if (day > maximum_day) return RtcReadResult::InvalidTime;
        // The supported century includes 2000 (a leap year), but not 2100.
        unsigned days = year * 365 + (year + 3) / 4 + day - 1;
        for (unsigned current = 1; current < month; ++current)
            days += kMonthDays[current - 1] + (current == 2 && year % 4 == 0);
        const int64_t seconds = int64_t(kEpoch2000) + int64_t(days) * 86400 +
                                hour * 3600 + minute * 60 + second;
        const time_t restored = time_t(seconds);
        if (int64_t(restored) != seconds) return RtcReadResult::InvalidTime;
        epoch = restored;
        return RtcReadResult::Valid;
    }

    bool writeUtc(time_t epoch) override {
        struct tm utc = {};
        if (!gmtime_r(&epoch, &utc) || utc.tm_year < 100 || utc.tm_year > 199) {
            LOGE("RTC", "PCF8563 UTC outside supported years 2000-2099");
            return false;
        }
        const uint8_t data[] = {
            encode_bcd(utc.tm_sec), encode_bcd(utc.tm_min), encode_bcd(utc.tm_hour),
            encode_bcd(utc.tm_mday), uint8_t(utc.tm_wday), encode_bcd(utc.tm_mon + 1),
            encode_bcd(utc.tm_year - 100)
        };
        BusLock lock;
        if (!lock.acquired) return false;
        const uint8_t stopped = kStop, running = 0;
        // Leave STOP set after an interrupted write so a partial calendar is invalid.
        return write_registers(kControlRegister, &stopped, 1) &&
               write_registers(kTimeRegister, data, sizeof(data)) &&
               write_registers(kControlRegister, &running, 1);
    }
};
}

RtcDriver* rtc_driver_create() {
    static Pcf8563RtcDriver driver;
    return &driver;
}
