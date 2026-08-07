#include "ha_stats_resample.h"

#include <math.h>
#include <stdlib.h>

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// days_from_civil). Avoids timegm(), which is not portable across the host
// test toolchain and the ESP32 newlib build.
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= (m <= 2);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);                    // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;        // [0, 146096]
    return era * 146097 + (int64_t)doe - 719468;
}

static bool read_uint(const char*& p, unsigned digits, unsigned* out) {
    unsigned v = 0;
    for (unsigned i = 0; i < digits; i++) {
        if (*p < '0' || *p > '9') return false;
        v = v * 10 + (unsigned)(*p++ - '0');
    }
    *out = v;
    return true;
}

uint64_t ha_stats_parse_iso8601(const char* s) {
    if (!s) return 0;
    const char* p = s;
    unsigned year, month, day, hour, minute, second;

    if (!read_uint(p, 4, &year)) return 0;
    if (*p++ != '-') return 0;
    if (!read_uint(p, 2, &month)) return 0;
    if (*p++ != '-') return 0;
    if (!read_uint(p, 2, &day)) return 0;
    if (*p != 'T' && *p != ' ') return 0;
    p++;
    if (!read_uint(p, 2, &hour)) return 0;
    if (*p++ != ':') return 0;
    if (!read_uint(p, 2, &minute)) return 0;
    if (*p++ != ':') return 0;
    if (!read_uint(p, 2, &second)) return 0;

    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 60) return 0;

    if (*p == '.') {                       // Fractional seconds — ignored
        p++;
        while (*p >= '0' && *p <= '9') p++;
    }

    int64_t offset_sec = 0;
    if (*p == '+' || *p == '-') {
        const int sign = (*p == '-') ? -1 : 1;
        p++;
        unsigned oh, om = 0;
        if (!read_uint(p, 2, &oh)) return 0;
        if (*p == ':') p++;
        if (*p >= '0' && *p <= '9') {
            if (!read_uint(p, 2, &om)) return 0;
        }
        offset_sec = sign * (int64_t)(oh * 3600 + om * 60);
    }

    const int64_t days = days_from_civil((int64_t)year, month, day);
    const int64_t secs = days * 86400 + (int64_t)hour * 3600 +
                         (int64_t)minute * 60 + (int64_t)second - offset_sec;
    return (secs < 0) ? 0 : (uint64_t)secs;
}

void ha_stats_request_window(uint32_t slot_ms, uint16_t slot_count,
                             uint64_t end_bucket,
                             uint64_t* start_sec, uint64_t* end_sec) {
    if (!start_sec || !end_sec || slot_ms == 0 || slot_count == 0) return;
    const uint64_t end_ms = (end_bucket + 1) * (uint64_t)slot_ms;
    const uint64_t start_ms = end_ms - (uint64_t)slot_count * slot_ms;
    *start_sec = start_ms / 1000ULL;
    *end_sec = end_ms / 1000ULL;
}

size_t ha_stats_resample(const HaStatPoint* points, size_t point_count,
                         uint64_t slot_ms, uint64_t end_bucket,
                         float* out, size_t out_count) {
    if (!out || out_count == 0) return 0;
    for (size_t i = 0; i < out_count; i++) out[i] = NAN;
    if (!points || point_count == 0 || slot_ms == 0) return 0;

    // Bucket id of out[0]. end_bucket may be smaller than out_count for
    // pathological clocks, in which case there is nothing to align against.
    if (end_bucket + 1 < out_count) return 0;
    const uint64_t first_bucket = end_bucket - (out_count - 1);

    for (size_t i = 0; i < point_count; i++) {
        uint64_t bucket = (points[i].start_sec * 1000ULL) / slot_ms;
        if (bucket < first_bucket || bucket > end_bucket) continue;
        if (!isfinite(points[i].value)) continue;
        out[bucket - first_bucket] = points[i].value;
    }

    size_t filled = 0;
    for (size_t i = 0; i < out_count; i++) {
        if (isfinite(out[i])) filled++;
    }
    return filled;
}

size_t ha_stats_merge(float* ring, size_t slot_count, size_t head,
                      size_t live_count, uint64_t newest_bucket,
                      const float* values, size_t value_count,
                      uint64_t end_bucket) {
    if (!ring || slot_count == 0) return live_count;
    if (live_count > slot_count) live_count = slot_count;
    if (live_count == slot_count) return live_count;   // Window already covered
    if (!values || value_count == 0) return live_count;
    if (newest_bucket + 1 < slot_count) return live_count;  // Grid not established

    // Logical positions run 0 (oldest) .. slot_count-1 (newest, = newest_bucket).
    // Live samples occupy the trailing `live_count` positions.
    const size_t free_end = slot_count - live_count;   // exclusive

    // Value for logical position p, or NAN when it falls outside the response.
    auto value_at = [&](size_t p) -> float {
        uint64_t bucket = newest_bucket - (uint64_t)(slot_count - 1 - p);
        if (bucket > end_bucket) return NAN;
        uint64_t back = end_bucket - bucket;
        if (back >= value_count) return NAN;
        return values[value_count - 1 - (size_t)back];
    };

    size_t first = free_end;
    for (size_t p = 0; p < free_end; p++) {
        if (isfinite(value_at(p))) { first = p; break; }
    }
    if (first >= free_end) return live_count;          // Nothing usable

    for (size_t p = first; p < free_end; p++) {
        ring[(head + p) % slot_count] = value_at(p);
    }
    return slot_count - first;
}
