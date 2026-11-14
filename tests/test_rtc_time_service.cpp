#include "../src/app/time_service.h"
#include "../src/app/i2c_bus.h"
#include <WiFi.h>
#include <Wire.h>
#include <sys/time.h>
#include <cassert>
#include <cstring>
#include <utility>
#if HAS_RTC
#include "../src/app/rtc_driver.h"
#endif

TwoWire Wire(0), Wire1(1);
HostWiFi WiFi;
bool sticky_test_unlocked_wire0 = false;
bool sticky_test_invalid_gauge_transaction = false;
static bool locked = false, fail_lock = false, ota_active = false, fail_set = false;
static time_t system_time = 0;
static unsigned system_sets = 0, ntp_starts = 0;
static int wifi_status = 0;
static void (*ntp_callback)(struct timeval*) = nullptr;

bool sticky_test_bus_locked() { return locked; }
void i2c_bus_init() {}
bool i2c_bus_lock(TickType_t) {
    if (fail_lock) return false;
    assert(!locked);
    locked = true;
    return true;
}
void i2c_bus_unlock() { assert(locked); locked = false; }
bool ota_activity_is_active() { return ota_active; }
int HostWiFi::status() const { return wifi_status; }
void esp_sntp_set_time_sync_notification_cb(void (*callback)(struct timeval*)) {
    ntp_callback = callback;
}
void configTzTime(const char*, const char*, const char*) { ++ntp_starts; }
extern "C" time_t time(time_t* output) noexcept {
    if (output) *output = system_time;
    return system_time;
}
extern "C" int settimeofday(const struct timeval* value, const struct timezone*) noexcept {
    ++system_sets;
    if (fail_set) return -1;
    system_time = value->tv_sec;
    return 0;
}

static void synchronize(time_t epoch) {
    system_time = epoch;
    struct timeval value = {epoch, 0};
    assert(ntp_callback);
    const unsigned transactions = Wire.transactions;
    ntp_callback(&value);
    assert(Wire.transactions == transactions);
}

#if HAS_RTC
static time_t utc(int year, int month, int day, int hour = 0, int minute = 0, int second = 0) {
    struct tm calendar = {};
    calendar.tm_year = year - 1900;
    calendar.tm_mon = month - 1;
    calendar.tm_mday = day;
    calendar.tm_hour = hour;
    calendar.tm_min = minute;
    calendar.tm_sec = second;
    return timegm(&calendar);
}

static void driver_tests(RtcDriver& driver) {
    assert(driver.begin());
    assert(Wire.begins == 0); // The driver must not reinitialize the shared bus.
    assert(time_service_set_timezone("Europe/Brussels"));
    for (const time_t epoch : {utc(2000, 2, 29, 23, 59, 59), utc(2026, 11, 14, 7, 36, 38),
                              utc(2040, 1, 1), utc(2099, 12, 31, 23, 59, 59)}) {
        assert(driver.writeUtc(epoch));
        time_t restored = 0;
        assert(driver.readUtc(restored) == RtcReadResult::Valid && restored == epoch);
    }
    assert(!driver.writeUtc(utc(2100, 1, 1)));
    assert(!driver.writeUtc(utc(1999, 12, 31)));
    assert(driver.writeUtc(utc(2026, 11, 14)));
    for (const auto invalid : {std::pair<unsigned, uint8_t>{2, 0x80}, {0, 0x20}, {0, 0x80},
                               {2, 0x6a}, {3, 0x60}, {4, 0x24}, {5, 0}, {6, 7}, {7, 0x91}}) {
        const uint8_t saved = Wire.registers[invalid.first];
        Wire.registers[invalid.first] = invalid.second;
        time_t epoch = 123;
        assert(driver.readUtc(epoch) == RtcReadResult::InvalidTime && epoch == 123);
        Wire.registers[invalid.first] = saved;
    }
    Wire.registers[7] = 2;
    Wire.registers[5] = 0x29; // 2026 is not a leap year.
    time_t epoch = 0;
    assert(driver.readUtc(epoch) == RtcReadResult::InvalidTime);
    assert(driver.writeUtc(utc(2026, 11, 14)));
    Wire.shortRegister = 0;
    assert(driver.readUtc(epoch) == RtcReadResult::IoError);
    Wire.shortRegister = -1;
    Wire.nackRegister = 0;
    assert(!driver.begin() && driver.readUtc(epoch) == RtcReadResult::IoError);
    Wire.nackRegister = -1;
    fail_lock = true;
    assert(!driver.begin() && driver.readUtc(epoch) == RtcReadResult::IoError);
    assert(!driver.writeUtc(utc(2026, 11, 14)));
    fail_lock = false;
    // Simulate a partially accepted calendar write, including clearing VL.
    Wire.failedByteRegister = 4;
    assert(!driver.writeUtc(utc(2026, 12, 25)));
    Wire.failedByteRegister = -1;
    assert(driver.readUtc(epoch) == RtcReadResult::InvalidTime);
    assert(!sticky_test_unlocked_wire0 && !locked);
}
#endif

int main(int argc, char** argv) {
    assert(!time_service_ready());
#if HAS_RTC
    RtcDriver& driver = *rtc_driver_create();
    driver_tests(driver);
    const time_t boot_time = utc(2026, 11, 14, 7, 36, 38);
    assert(driver.writeUtc(boot_time));
    const char* mode = argc > 1 ? argv[1] : "valid";
    if (!strcmp(mode, "invalid")) Wire.registers[2] |= 0x80;
    if (!strcmp(mode, "old")) assert(driver.writeUtc(utc(2023, 12, 31)));
    if (!strcmp(mode, "io")) Wire.failRead = true;
    if (!strcmp(mode, "set-failure")) fail_set = true;
    const bool valid = !strcmp(mode, "valid");
    time_service_init_rtc();
    assert(time_service_ready() == valid);
    assert(system_time == (valid ? boot_time : 0));
    assert(ntp_starts == 0);
    if (valid) {
        char output[32];
        assert(time_service_format(system_time, "%H:%M", nullptr, output, sizeof(output)));
        assert(!strcmp(output, "08:36"));
    }
    Wire.failRead = false;
    fail_set = false;
#else
    (void)argc;
    (void)argv;
#endif
    time_service_loop();
    assert(ntp_starts == 0);
    wifi_status = WL_CONNECTED;
    time_service_loop();
    assert(ntp_starts == 1);
    const time_t network_time = 1800000000;
    synchronize(network_time);
    assert(time_service_ready());
#if HAS_RTC
    ota_active = true;
    const unsigned transactions = Wire.transactions;
    time_service_loop();
    assert(Wire.transactions == transactions);
    // A newer sync and elapsed time during OTA must not persist an old callback time.
    synchronize(network_time + 10);
    system_time += 100;
    ota_active = false;
    time_service_loop();
    time_t retained = 0;
    assert(driver.readUtc(retained) == RtcReadResult::Valid && retained == system_time);
    const unsigned completed = Wire.transactions;
    time_service_loop();
    assert(Wire.transactions == completed); // No polling or repeated writes.
    Wire.failWrite = true;
    synchronize(network_time + 200);
    time_service_loop();
    assert(time_service_ready() && system_time == network_time + 200);
    Wire.failWrite = false;
    synchronize(network_time + 300);
    time_service_loop();
    assert(driver.readUtc(retained) == RtcReadResult::Valid && retained == system_time);
    assert(!locked && !sticky_test_unlocked_wire0);
#else
    assert(Wire.transactions == 0 && system_sets == 0);
#endif
    wifi_status = 0;
    time_service_loop();
    assert(time_service_ready());
    return 0;
}
