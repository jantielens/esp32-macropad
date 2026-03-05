#include "time_binding.h"
#include "board_config.h"

#if HAS_DISPLAY

#include "binding_template.h"
#include "log_manager.h"

#include <Arduino.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#define TAG "TimeBind"

// ============================================================================
// Olson → POSIX TZ lookup table
// ============================================================================

struct TzEntry {
    const char* olson;
    const char* posix;
};

static const TzEntry TZ_TABLE[] = {
    // UTC
    {"UTC",                    "UTC0"},

    // Europe
    {"Europe/London",          "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Amsterdam",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin",          "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Brussels",        "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Paris",           "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome",            "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid",          "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Zurich",          "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Vienna",          "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Stockholm",       "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Oslo",            "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Copenhagen",      "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Warsaw",          "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Helsinki",        "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Athens",          "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Bucharest",       "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Istanbul",        "TRT-3"},
    {"Europe/Moscow",          "MSK-3"},

    // Americas
    {"America/New_York",       "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago",        "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver",         "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles",    "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Anchorage",      "AKST9AKDT,M3.2.0,M11.1.0"},
    {"America/Phoenix",        "MST7"},
    {"America/Toronto",        "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Vancouver",      "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Sao_Paulo",      "<-03>3"},
    {"America/Argentina/Buenos_Aires", "<-03>3"},
    {"America/Mexico_City",    "CST6CDT,M4.1.0,M10.5.0"},

    // Asia / Pacific
    {"Asia/Tokyo",             "JST-9"},
    {"Asia/Shanghai",          "CST-8"},
    {"Asia/Hong_Kong",         "HKT-8"},
    {"Asia/Singapore",         "SGT-8"},
    {"Asia/Seoul",             "KST-9"},
    {"Asia/Kolkata",           "IST-5:30"},
    {"Asia/Dubai",             "GST-4"},
    {"Asia/Riyadh",            "AST-3"},
    {"Asia/Bangkok",           "ICT-7"},
    {"Asia/Jakarta",           "WIB-7"},

    // Oceania
    {"Australia/Sydney",       "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Melbourne",    "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Perth",        "AWST-8"},
    {"Pacific/Auckland",       "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Pacific/Honolulu",       "HST10"},

    // Africa
    {"Africa/Cairo",           "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"Africa/Johannesburg",    "SAST-2"},
    {"Africa/Lagos",           "WAT-1"},
};

static const size_t TZ_TABLE_SIZE = sizeof(TZ_TABLE) / sizeof(TZ_TABLE[0]);

// Look up an Olson name and return the POSIX string, or nullptr if not found.
static const char* lookup_posix_tz(const char* name) {
    for (size_t i = 0; i < TZ_TABLE_SIZE; i++) {
        if (strcasecmp(TZ_TABLE[i].olson, name) == 0) return TZ_TABLE[i].posix;
    }
    return nullptr;
}

// ============================================================================
// Resolve the effective POSIX TZ string from a user-supplied timezone param.
// Tries Olson lookup first, then falls back to treating it as raw POSIX.
// ============================================================================

static const char* resolve_tz(const char* tz_param) {
    if (!tz_param || !tz_param[0]) return "UTC0";
    const char* posix = lookup_posix_tz(tz_param);
    return posix ? posix : tz_param;  // fallback: treat as raw POSIX
}

// ============================================================================
// Scheme resolver — called by binding_template_resolve()
// ============================================================================

static bool time_binding_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !params[0]) {
        strlcpy(out, "ERR:no fmt", out_len);
        return false;
    }

    // Parse: "format" or "format;timezone"
    char fmt[64];
    char tz[64];
    fmt[0] = '\0';
    tz[0] = '\0';

    const char* sep = strchr(params, ';');
    if (!sep) {
        strlcpy(fmt, params, sizeof(fmt));
    } else {
        size_t flen = (size_t)(sep - params);
        if (flen >= sizeof(fmt)) flen = sizeof(fmt) - 1;
        memcpy(fmt, params, flen);
        fmt[flen] = '\0';
        strlcpy(tz, sep + 1, sizeof(tz));
    }

    // Check if NTP has synced (time > 2024-01-01)
    time_t now = time(nullptr);
    if (now < 1704067200L) {
        strlcpy(out, "--:--", out_len);
        return true;
    }

    // Apply timezone
    const char* posix_tz = resolve_tz(tz);
    setenv("TZ", posix_tz, 1);
    tzset();

    struct tm ti;
    localtime_r(&now, &ti);

    if (strftime(out, out_len, fmt, &ti) == 0) {
        strlcpy(out, "ERR:fmt", out_len);
        return false;
    }
    return true;
}

// ============================================================================
// No-op topic collector — time data is local, no subscriptions needed
// ============================================================================

static void time_binding_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ============================================================================
// Public API
// ============================================================================

void time_binding_init() {
    if (!binding_template_register("time", time_binding_resolve, time_binding_collect)) {
        LOGE(TAG, "Failed to register time binding scheme");
    }
}

void time_binding_start_ntp() {
    configTime(0, 0, "pool.ntp.org");
    LOGI(TAG, "NTP sync started (pool.ntp.org)");
}

#else // !HAS_DISPLAY

void time_binding_init() {}
void time_binding_start_ntp() {}

#endif
