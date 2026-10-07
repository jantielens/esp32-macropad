#include "../src/app/time_service.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <cstring>
#include "binding_template.h"
#include "time_binding.h"

static bool calendar_clock_ready = true;
extern "C" unsigned long millis() { return 1000; }
bool calendar_time_ready() { return calendar_clock_ready; }
#define time_service_ready calendar_time_ready
#include "../src/app/time_binding.cpp"
#undef time_service_ready

static time_t utc(int year, int month, int day, int hour, int minute) {
    struct tm value = {};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    return timegm(&value);
}

int main() {
    assert(time_service_set_timezone("Europe/Brussels"));
    size_t timezone_count = 0;
    const TimezoneEntry* catalog = time_service_timezones(&timezone_count);
    assert(timezone_count >= 50 && timezone_count <= 70);
    for (size_t index = 0; index < timezone_count; ++index) {
        assert(time_service_timezone_valid(catalog[index].name));
        assert(time_service_timezone_valid(catalog[index].posix));
    }
    char preview[48];
    const uint32_t generation = time_service_generation();
    assert(time_service_format(utc(2026, 10, 7, 5, 0), "%Y-%m-%d %H:%M:%S %z", "Asia/Kathmandu", preview, sizeof(preview)));
    assert(!strcmp(preview, "2026-10-07 10:45:00 +0545"));
    assert(time_service_generation() == generation);
    assert(time_service_format(utc(2026, 7, 7, 5, 0), "%H:%M %z", "America/Mexico_City", preview, sizeof(preview)));
    assert(!strcmp(preview, "23:00 -0600"));
    assert(!time_service_timezone_valid("Europe/Unknown"));
    const char* valid[] = {"UTC", "UTC0", "<+03>-3", "IST-5:30", "EST5EDT,M3.2.0,M11.1.0", "EET-2EEST,M4.5.5/0,M10.5.4/24", "ABC1DEF,J60/2,300/3"};
    const char* invalid[] = {"", "XYZ", "UTC25", "UTC0:60", "AB0", "<+03-3", "ABC1junk", "ABC1DEF,M13.1.0,M10.5.0", "ABC1DEF,M3.6.0,M10.5.0", "ABC1DEF,M3.2.7,M10.5.0", "ABC1DEF,J0,J365", "UTC0,garbage"};
    for (const char* value : valid) assert(time_service_timezone_valid(value));
    for (const char* value : invalid) assert(!time_service_timezone_valid(value));
    const time_t repeated = utc(2026, 10, 25, 0, 30);
    assert(time_service_alarm_candidate(utc(2026, 10, 25, 1, 30), 2, 30, 1) == repeated);
    assert(time_service_alarm_candidate(utc(2026, 3, 29, 1, 30), 2, 30, 1) == 0);
    assert(time_service_alarm_candidate(utc(2026, 10, 7, 5, 0), 7, 0, 1 << 3) == utc(2026, 10, 7, 5, 0));
    assert(time_service_alarm_candidate(utc(2026, 10, 7, 5, 0), 7, 0, 1 << 4) == 0);
    assert(time_service_alarm_candidate(utc(2026, 10, 11, 22, 2), 0, 0, 1 << 1) == utc(2026, 10, 11, 22, 0));
    assert(time_service_alarm_next(utc(2026, 10, 7, 4, 59), 7, 0) == utc(2026, 10, 7, 5, 0));
    assert(time_service_alarm_next(utc(2026, 10, 7, 5, 0), 7, 0) == utc(2026, 10, 8, 5, 0));
    assert(time_service_alarm_next(utc(2026, 12, 31, 23, 30), 7, 0) == utc(2027, 1, 1, 6, 0));
    assert(time_service_alarm_next(utc(2026, 3, 28, 23, 0), 2, 30) == utc(2026, 3, 30, 0, 30));
    assert(time_service_alarm_next(utc(2026, 10, 24, 23, 0), 2, 30) == repeated);
    assert(time_service_alarm_next(utc(2026, 10, 25, 1, 0), 2, 30) == utc(2026, 10, 26, 1, 30));
    assert(time_service_alarm_next(utc(2026, 10, 7, 4, 59), 24, 0) == 0);
    assert(time_service_alarm_next(utc(2026, 10, 7, 4, 59), 7, 60) == 0);
    std::thread clock([] {
        for (int count = 0; count < 1000; ++count) {
            char text[32];
            assert(time_service_format(utc(2026, 10, 7, 5, 0), "%H:%M", "Asia/Tokyo", text, sizeof(text)));
            assert(!strcmp(text, "14:00"));
        }
    });
    for (int count = 0; count < 1000; ++count)
        assert(time_service_alarm_candidate(utc(2026, 10, 7, 5, 0), 7, 0, 127) == utc(2026, 10, 7, 5, 0));
    clock.join();
    time_binding_init();
    auto binding_expect = [](const char* params, const char* expected) {
        char value[32] = {};
        assert(binding_template_resolve_registered("time", 4, params, value, sizeof(value)) == BINDING_RESOLVER_RESOLVED);
        assert(!strcmp(value, expected));
    };
    assert(time_service_set_timezone("UTC0"));
    binding_expect("%z", "+0000");
    binding_expect("%z;", "+0000");
    assert(time_service_set_timezone("IST-5:30"));
    binding_expect("%z", "+0530");
    binding_expect("%z;", "+0530");
    binding_expect("%z;Asia/Kathmandu", "+0545");
    binding_expect("%z", "+0530");
    calendar_clock_ready = false;
    binding_expect("%z", "--:--");
    binding_expect("%ums", "1000");
    assert(!time_service_ready());
    std::puts("alarm calendar: PASS");
}