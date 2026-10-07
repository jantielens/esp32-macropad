#include "time_service.h"
#include <atomic>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>
#if defined(ARDUINO)
#include <Arduino.h>
#include "board_config.h"
#include <WiFi.h>
#include <esp_sntp.h>
#include "log_manager.h"
#endif

static const TimezoneEntry timezones[] = {
    {"UTC", "UTC0"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Brussels", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Vienna", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Oslo", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Copenhagen", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Bucharest", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Istanbul", "TRT-3"}, {"Europe/Moscow", "MSK-3"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"America/Phoenix", "MST7"}, {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Sao_Paulo", "<-03>3"}, {"America/Argentina/Buenos_Aires", "<-03>3"},
    {"America/Mexico_City", "CST6"},
    {"America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"America/Bogota", "<-05>5"}, {"America/Halifax", "AST4ADT,M3.2.0,M11.1.0"},
    {"Asia/Tokyo", "JST-9"}, {"Asia/Shanghai", "CST-8"}, {"Asia/Hong_Kong", "HKT-8"},
    {"Asia/Singapore", "SGT-8"}, {"Asia/Seoul", "KST-9"}, {"Asia/Kolkata", "IST-5:30"},
    {"Asia/Dubai", "GST-4"}, {"Asia/Riyadh", "AST-3"}, {"Asia/Bangkok", "ICT-7"},
    {"Asia/Jakarta", "WIB-7"}, {"Asia/Kathmandu", "<+0545>-5:45"},
    {"Asia/Dhaka", "<+06>-6"}, {"Asia/Yangon", "<+0630>-6:30"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Perth", "AWST-8"}, {"Australia/Darwin", "ACST-9:30"},
    {"Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Pacific/Chatham", "<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45"},
    {"Pacific/Noumea", "<+11>-11"}, {"Pacific/Pago_Pago", "SST11"},
    {"Pacific/Kiritimati", "<+14>-14"},
    {"Pacific/Honolulu", "HST10"}, {"Africa/Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"Africa/Johannesburg", "SAST-2"}, {"Africa/Lagos", "WAT-1"}, {"Africa/Nairobi", "EAT-3"}
};
static std::mutex timezone_mutex;
static char device_timezone[64] = "UTC0";
static std::atomic<uint32_t> timezone_generation{0};
static std::atomic<bool> synchronized{false};

const TimezoneEntry* time_service_timezones(size_t* count) {
    if (count) *count = sizeof(timezones) / sizeof(timezones[0]);
    return timezones;
}

static const char* resolve_timezone(const char* timezone) {
    if (!timezone || !timezone[0]) return device_timezone;
    for (const auto& entry : timezones)
        if (!strcasecmp(timezone, entry.name)) return entry.posix;
    return timezone;
}

static bool timezone_number(const char*& cursor, unsigned minimum, unsigned maximum) {
    if (!isdigit(static_cast<unsigned char>(*cursor))) return false;
    unsigned value = 0;
    while (isdigit(static_cast<unsigned char>(*cursor))) {
        value = value * 10 + (*cursor++ - '0');
        if (value > maximum) return false;
    }
    return value >= minimum;
}

static bool timezone_name(const char*& cursor) {
    const bool quoted = *cursor == '<';
    if (quoted) ++cursor;
    const char* start = cursor;
    while (isalpha(static_cast<unsigned char>(*cursor))
        || (quoted && (isdigit(static_cast<unsigned char>(*cursor)) || *cursor == '+' || *cursor == '-'))) ++cursor;
    if (cursor - start < 3) return false;
    return !quoted || *cursor++ == '>';
}

static bool timezone_offset(const char*& cursor, unsigned maximum_hour) {
    if (*cursor == '+' || *cursor == '-') ++cursor;
    if (!timezone_number(cursor, 0, maximum_hour)) return false;
    for (unsigned part = 0; part < 2 && *cursor == ':'; ++part) {
        ++cursor;
        if (!timezone_number(cursor, 0, 59)) return false;
    }
    return true;
}

static bool timezone_rule(const char*& cursor) {
    if (*cursor == 'M') {
        ++cursor;
        if (!timezone_number(cursor, 1, 12) || *cursor++ != '.'
            || !timezone_number(cursor, 1, 5) || *cursor++ != '.'
            || !timezone_number(cursor, 0, 6)) return false;
    } else if (*cursor == 'J') {
        ++cursor;
        if (!timezone_number(cursor, 1, 365)) return false;
    } else if (!timezone_number(cursor, 0, 365)) return false;
    if (*cursor == '/') {
        ++cursor;
        if (!timezone_offset(cursor, 167)) return false;
    }
    return true;
}

bool time_service_timezone_valid(const char* timezone) {
    if (!timezone || !timezone[0] || strlen(timezone) >= sizeof(device_timezone)) return false;
    for (const auto& entry : timezones)
        if (!strcasecmp(timezone, entry.name)) return true;
    const char* cursor = timezone;
    if (!timezone_name(cursor) || !timezone_offset(cursor, 24)) return false;
    if (!*cursor) return true;
    if (!timezone_name(cursor)) return false;
    if (*cursor != ',' && !timezone_offset(cursor, 24)) return false;
    if (*cursor++ != ',' || !timezone_rule(cursor) || *cursor++ != ',' || !timezone_rule(cursor)) return false;
    return !*cursor;
}

bool time_service_set_timezone(const char* timezone) {
    if (!time_service_timezone_valid(timezone)) return false;
    std::lock_guard<std::mutex> lock(timezone_mutex);
    const char* posix = resolve_timezone(timezone);
    if (strcmp(posix, device_timezone)) {
        strcpy(device_timezone, posix);
        timezone_generation.fetch_add(1);
#if defined(ARDUINO)
        LOGI("Time", "Device timezone=%s resolved=%s", timezone, device_timezone);
#endif
    }
    setenv("TZ", device_timezone, 1);
    tzset();
    return true;
}

uint32_t time_service_generation() { return timezone_generation.load(); }
bool time_service_ready() { return synchronized.load() && time(nullptr) >= 1704067200; }

void time_service_start_ntp(void (*observer)(struct timeval*)) {
#if defined(ARDUINO)
    static bool started = false;
    static std::atomic<void (*)(struct timeval*)> sync_observer{nullptr};
    std::lock_guard<std::mutex> lock(timezone_mutex);
    if (started && !observer) return;
    sync_observer.store(observer);
    esp_sntp_set_time_sync_notification_cb([](struct timeval* value) {
        synchronized.store(true);
        LOGI("Time", "NTP synchronized: epoch=%lld", static_cast<long long>(value ? value->tv_sec : time(nullptr)));
        if (auto callback = sync_observer.load()) callback(value);
    });
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", device_timezone, 1);
    tzset();
    started = true;
    LOGI("Time", "NTP started: pool.ntp.org, time.nist.gov; timezone=%s", device_timezone);
#else
    (void)observer;
#endif
}

void time_service_loop() {
#if defined(ARDUINO) && !IS_EPAPER_FRAME
    if (WiFi.status() == WL_CONNECTED) time_service_start_ntp();
#endif
}

bool time_service_localtime(time_t epoch, struct tm* result) {
    std::lock_guard<std::mutex> lock(timezone_mutex);
    return result && localtime_r(&epoch, result);
}

bool time_service_format(time_t epoch, const char* format, const char* timezone,
                         char* out, size_t capacity) {
    std::lock_guard<std::mutex> lock(timezone_mutex);
    setenv("TZ", resolve_timezone(timezone), 1);
    tzset();
    struct tm local = {};
    const bool ok = localtime_r(&epoch, &local) && strftime(out, capacity, format, &local);
    setenv("TZ", device_timezone, 1);
    tzset();
    return ok;
}

static time_t alarm_local_occurrence(const struct tm& date, uint8_t hour, uint8_t minute) {
    time_t first = 0;
    for (int dst = 0; dst <= 1; ++dst) {
        struct tm candidate = date;
        candidate.tm_hour = hour;
        candidate.tm_min = minute;
        candidate.tm_isdst = dst;
        const time_t epoch = mktime(&candidate);
        struct tm checked = {};
        if (epoch <= 0 || !localtime_r(&epoch, &checked)) continue;
        if (checked.tm_year != date.tm_year || checked.tm_mon != date.tm_mon
            || checked.tm_mday != date.tm_mday || checked.tm_hour != hour
            || checked.tm_min != minute) continue;
        if (!first || epoch < first) first = epoch;
    }
    return first;
}

time_t time_service_alarm_candidate(time_t now, uint8_t hour, uint8_t minute, uint8_t weekdays) {
    if (hour > 23 || minute > 59 || !weekdays) return 0;
    std::lock_guard<std::mutex> lock(timezone_mutex);
    struct tm today = {};
    if (!localtime_r(&now, &today)) return 0;
    time_t latest = 0;
    for (int offset = -1; offset <= 0; ++offset) {
        struct tm date = today;
        date.tm_mday += offset;
        date.tm_hour = 12;
        date.tm_min = date.tm_sec = 0;
        date.tm_isdst = -1;
        mktime(&date);
        if (!(weekdays & (1U << date.tm_wday))) continue;
        const time_t first = alarm_local_occurrence(date, hour, minute);
        if (first && first <= now && first > latest) latest = first;
    }
    return latest;
}

time_t time_service_alarm_next(time_t now, uint8_t hour, uint8_t minute, uint8_t weekdays) {
    if (hour > 23 || minute > 59 || !weekdays || weekdays > 127) return 0;
    std::lock_guard<std::mutex> lock(timezone_mutex);
    struct tm today = {};
    if (!localtime_r(&now, &today)) return 0;
    for (int offset = 0; offset < 15; ++offset) {
        struct tm date = today;
        date.tm_mday += offset;
        date.tm_hour = 12;
        date.tm_min = date.tm_sec = 0;
        date.tm_isdst = -1;
        mktime(&date);
        if (!(weekdays & (1U << date.tm_wday))) continue;
        const time_t occurrence = alarm_local_occurrence(date, hour, minute);
        if (occurrence > now) return occurrence;
    }
    return 0;
}