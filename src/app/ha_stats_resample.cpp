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

static bool leap_year(unsigned year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

uint64_t ha_stats_parse_iso8601_ms(const char* s) {
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

    if (month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) return 0;

    static const unsigned days_in_month[] = {31, 28, 31, 30, 31, 30,
                                              31, 31, 30, 31, 30, 31};
    unsigned max_day = days_in_month[month - 1];
    if (month == 2 && leap_year(year)) max_day++;
    if (day < 1 || day > max_day) return 0;

    unsigned milliseconds = 0;
    if (*p == '.') {
        p++;
        unsigned digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (digits < 3) milliseconds = milliseconds * 10 + (unsigned)(*p - '0');
            digits++;
            p++;
        }
        if (digits == 0) return 0;
        while (digits < 3) { milliseconds *= 10; digits++; }
    }

    int64_t offset_sec = 0;
    if (*p == '+' || *p == '-') {
        const int sign = (*p == '-') ? -1 : 1;
        p++;
        unsigned oh, om = 0;
        if (!read_uint(p, 2, &oh)) return 0;
        if (*p == ':') p++;
        if (!read_uint(p, 2, &om) || oh > 23 || om > 59) return 0;
        offset_sec = sign * (int64_t)(oh * 3600 + om * 60);
    } else if (*p == 'Z') {
        p++;
    }
    if (*p != '\0') return 0;

    const int64_t days = days_from_civil((int64_t)year, month, day);
    const int64_t secs = days * 86400 + (int64_t)hour * 3600 +
                         (int64_t)minute * 60 + (int64_t)second - offset_sec;
    if (secs < 0) return 0;
    return (uint64_t)secs * 1000ULL + milliseconds;
}

uint64_t ha_stats_parse_iso8601(const char* s) {
    return ha_stats_parse_iso8601_ms(s) / 1000ULL;
}

uint64_t ha_history_raw_start_ms(uint32_t slot_ms, uint16_t slot_count,
                                  uint64_t end_bucket, uint16_t raw_hours) {
    const uint64_t end_ms = (end_bucket + 1) * (uint64_t)slot_ms;
    const uint64_t window_ms = (uint64_t)slot_ms * slot_count;
    const uint64_t limit_ms = (uint64_t)raw_hours * 3600000ULL;
    return end_ms - (raw_hours && limit_ms < window_ms ? limit_ms : window_ms);
}

bool ha_raw_history_sampler_init(HaRawHistorySampler* sampler, float* values,
                                 uint16_t slot_count, uint32_t slot_ms,
                                 uint64_t start_ms, uint64_t end_ms,
                                 uint64_t minimum_timestamp_ms) {
    if (!sampler || !values || !slot_count || !slot_ms || end_ms < start_ms) return false;
    *sampler = {};
    sampler->values = values;
    sampler->slot_count = slot_count;
    sampler->slot_ms = slot_ms;
    sampler->start_ms = start_ms;
    sampler->end_ms = end_ms;
    sampler->minimum_timestamp_ms = minimum_timestamp_ms;
    sampler->current_value = NAN;
    for (uint16_t i = 0; i < slot_count; i++) values[i] = NAN;
    return true;
}

bool ha_raw_history_sampler_add(HaRawHistorySampler* sampler,
                                uint64_t timestamp_ms, float value) {
    if (!sampler || !sampler->values) return false;
    if (sampler->has_timestamp && timestamp_ms < sampler->last_timestamp_ms) return false;
    sampler->has_timestamp = true;
    sampler->last_timestamp_ms = timestamp_ms;
    if (timestamp_ms > sampler->end_ms) return true;
    // Recorder's initial state may have changed before the requested suffix.
    if (timestamp_ms < sampler->minimum_timestamp_ms)
        timestamp_ms = sampler->minimum_timestamp_ms;

    uint16_t index = 0;
    if (timestamp_ms >= sampler->start_ms) {
        const uint64_t offset = timestamp_ms - sampler->start_ms;
        const uint64_t bucket = offset / sampler->slot_ms;
        if (bucket >= sampler->slot_count) return true;
        index = (uint16_t)bucket;
    }

    if (!sampler->has_coverage) {
        sampler->coverage_start_index = index;
        sampler->has_coverage = true;
    }
    if (!sampler->has_current) {
        sampler->current_index = index;
        sampler->has_current = true;
    } else if (index > sampler->current_index) {
        for (uint16_t i = sampler->current_index + 1; i < index; i++) {
            sampler->values[i] = sampler->current_value;
        }
        sampler->current_index = index;
    }

    sampler->current_value = value;
    sampler->values[index] = value;
    return true;
}

uint16_t ha_raw_history_sampler_finish(HaRawHistorySampler* sampler) {
    if (!sampler || !sampler->values || sampler->end_ms < sampler->start_ms) return 0;
    if (!sampler->has_current) return sampler->slot_count;
    uint64_t last_index = (sampler->end_ms - sampler->start_ms) / sampler->slot_ms;
    if (last_index >= sampler->slot_count) last_index = sampler->slot_count - 1;
    for (uint16_t i = sampler->current_index + 1; i <= last_index; i++) {
        sampler->values[i] = sampler->current_value;
    }
    return sampler->has_coverage ? sampler->coverage_start_index : sampler->slot_count;
}

void ha_history_discard_incomplete_raw(float* values, uint16_t slot_count) {
    if (!values) return;
    for (uint16_t i = 0; i < slot_count; i++) values[i] = NAN;
}

size_t ha_history_merge_older_statistics(float* raw_values,
                                         const float* statistics_values,
                                         uint16_t slot_count,
                                         uint16_t raw_coverage_start) {
    if (!raw_values || !statistics_values || !slot_count) return 0;
    if (raw_coverage_start > slot_count) raw_coverage_start = slot_count;
    size_t filled = 0;
    for (uint16_t i = 0; i < raw_coverage_start; i++) {
        if (!isfinite(raw_values[i]) && isfinite(statistics_values[i])) {
            raw_values[i] = statistics_values[i];
            filled++;
        }
    }
    return filled;
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

HaStatisticsWindow ha_history_statistics_window(uint32_t slot_ms, uint16_t slot_count,
                                                uint64_t end_bucket,
                                                uint16_t prefix_slots) {
    const uint64_t window_ms = (uint64_t)slot_ms * slot_count;
    const uint32_t period_sec = slot_ms >= 3600000UL || window_ms > 86400000ULL
                                   ? 3600 : 300;
    const uint64_t end_ms = (end_bucket + 1) * (uint64_t)slot_ms;
    const uint64_t start_ms = end_ms - window_ms;
    if (prefix_slots > slot_count) prefix_slots = slot_count;
    const uint64_t cutoff_ms = start_ms + (uint64_t)prefix_slots * slot_ms;
    return {start_ms / (period_sec * 1000ULL) * period_sec,
            cutoff_ms / 1000 + (cutoff_ms % 1000 != 0), period_sec};
}

HaHistoryFetchOutcome ha_history_fetch_outcome(HaHistorySourceOutcome raw,
                                               HaHistorySourceOutcome statistics,
                                               bool need_statistics,
                                               bool has_usable_history) {
    using Source = HaHistorySourceOutcome;
    using Fetch = HaHistoryFetchOutcome;
    if (raw == Source::Cancelled || (need_statistics && statistics == Source::Cancelled))
        return Fetch::Cancelled;
    if (raw == Source::AuthFailure ||
        (raw != Source::Success && need_statistics && statistics == Source::AuthFailure))
        return Fetch::PermanentFailure;
    if (raw == Source::Retryable || (need_statistics && statistics == Source::Retryable))
        return has_usable_history ? Fetch::PartialRetryable : Fetch::RetryableFailure;
    if (raw == Source::Success || (need_statistics && statistics == Source::Success))
        return Fetch::Success;
    return Fetch::PermanentFailure;
}

size_t ha_stats_resample(const HaStatPoint* points, size_t point_count,
                         uint64_t slot_ms, uint64_t source_period_ms,
                         uint64_t end_bucket,
                         float* out, size_t out_count) {
    if (!out || out_count == 0) return 0;
    for (size_t i = 0; i < out_count; i++) out[i] = NAN;
    if (!points || point_count == 0 || slot_ms == 0 || source_period_ms == 0) return 0;

    // Bucket id of out[0]. end_bucket may be smaller than out_count for
    // pathological clocks, in which case there is nothing to align against.
    if (end_bucket + 1 < out_count) return 0;
    const uint64_t first_bucket = end_bucket - (out_count - 1);

    size_t filled = 0;
    for (size_t i = 0; i < out_count; i++) {
        const uint64_t sample_ms = (first_bucket + i + 1) * slot_ms - 1;
        uint64_t latest_start = 0;
        bool found = false;
        for (size_t j = 0; j < point_count; j++) {
            const uint64_t start_ms = points[j].start_sec * 1000ULL;
            if (start_ms > sample_ms || sample_ms - start_ms >= source_period_ms)
                continue;
            if (!found || start_ms >= latest_start) {
                out[i] = points[j].value;
                latest_start = start_ms;
                found = true;
            }
        }
        if (isfinite(out[i])) filled++;
    }
    return filled;
}

size_t ha_stats_merge(float* ring, size_t slot_count, size_t head,
                      size_t live_count, uint64_t newest_bucket,
                      const float* values, size_t value_count,
                      uint64_t end_bucket, size_t existing_count) {
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

    if (existing_count > slot_count) existing_count = slot_count;
    size_t first = existing_count > live_count ? slot_count - existing_count : free_end;
    for (size_t p = 0; p < first; p++) {
        if (isfinite(value_at(p))) { first = p; break; }
    }
    if (first >= free_end) return live_count;          // Nothing usable

    for (size_t p = first; p < free_end; p++) {
        ring[(head + p) % slot_count] = value_at(p);
    }
    return slot_count - first;
}
