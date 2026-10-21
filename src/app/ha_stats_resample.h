#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Home Assistant statistics resampling (pure, dependency-free)
// ============================================================================
// Maps Recorder statistic periods onto a data stream's fixed slot grid and
// merges the result into a ring buffer. Kept free of Arduino, LVGL and PSRAM
// dependencies so the arithmetic can be covered by host tests.
//
// Bucket ids are floor(unix_ms / slot_ms) — the same grid data_stream.cpp uses
// once the system clock is NTP-valid.

// One Recorder statistic period.
struct HaStatPoint {
    uint64_t start_sec;   // Period start, unix seconds
    float    value;       // Statistic value (mean / state / sum)
};

constexpr uint32_t HA_HISTORY_MAX_WINDOW_SECS = 30UL * 86400UL;
// Thirty days of hourly periods, with headroom for overlapping boundaries.
constexpr size_t HA_STATISTICS_MAX_POINTS = HA_HISTORY_MAX_WINDOW_SECS / 3600 + 2;
constexpr size_t HA_STATISTICS_JSON_BYTES = 128 * 1024;
constexpr uint16_t HA_HISTORY_MAX_RAW_HOURS = HA_HISTORY_MAX_WINDOW_SECS / 3600;

// Zero requests the full chart window; positive hours cap the recent suffix.
uint64_t ha_history_raw_start_ms(uint32_t slot_ms, uint16_t slot_count,
                                  uint64_t end_bucket, uint16_t raw_hours);

struct HaStatisticsWindow {
    uint64_t start_sec;
    uint64_t end_sec;
    uint32_t period_sec;
};

// Include the aggregate overlapping the first output bucket. prefix_slots
// excludes every bucket covered by raw history, including explicit raw gaps.
HaStatisticsWindow ha_history_statistics_window(uint32_t slot_ms, uint16_t slot_count,
                                                uint64_t end_bucket,
                                                uint16_t prefix_slots);

enum class HaHistorySourceOutcome : uint8_t {
    Success, Retryable, Permanent, AuthFailure, Cancelled
};

enum class HaHistoryFetchOutcome : uint8_t {
    Success, PartialRetryable, RetryableFailure, PermanentFailure, Cancelled
};

// A successful empty source is resolved; a transiently failed required source
// is not. Partial results may be displayed but must not finalize hydration.
HaHistoryFetchOutcome ha_history_fetch_outcome(HaHistorySourceOutcome raw,
                                               HaHistorySourceOutcome statistics,
                                               bool need_statistics,
                                               bool has_usable_history);

// Parse a Recorder period start timestamp into unix seconds.
//
// Home Assistant serializes these as ISO 8601 — "2026-07-21T10:05:00+00:00",
// a trailing "Z", and fractional seconds are all accepted, and a numeric UTC
// offset is applied. Returns 0 when the input cannot be parsed.
uint64_t ha_stats_parse_iso8601(const char* s);

// Parse an ISO 8601 timestamp to unix milliseconds. Fractional digits beyond
// milliseconds are truncated, never rounded across a bucket boundary.
uint64_t ha_stats_parse_iso8601_ms(const char* s);

// Incrementally sample Recorder state changes onto the requested bucket grid.
// NAN values represent explicitly unavailable or nonnumeric states. Records
// must be timestamp ordered; equal timestamps are accepted and the last wins.
// `minimum_timestamp_ms` bounds initial-state coverage for a capped raw request.
struct HaRawHistorySampler {
    float* values;
    uint16_t slot_count;
    uint32_t slot_ms;
    uint64_t start_ms;
    uint64_t end_ms;
    uint64_t minimum_timestamp_ms;
    uint64_t last_timestamp_ms;
    uint16_t current_index;
    uint16_t coverage_start_index;
    float current_value;
    bool has_timestamp;
    bool has_current;
    bool has_coverage;
};

bool ha_raw_history_sampler_init(HaRawHistorySampler* sampler, float* values,
                                 uint16_t slot_count, uint32_t slot_ms,
                                 uint64_t start_ms, uint64_t end_ms,
                                 uint64_t minimum_timestamp_ms = 0);
bool ha_raw_history_sampler_add(HaRawHistorySampler* sampler,
                                uint64_t timestamp_ms, float value);
uint16_t ha_raw_history_sampler_finish(HaRawHistorySampler* sampler);

// Discard sampled values when a raw-history response is incomplete. Until the
// response finishes, those values may cover only its oldest records.
void ha_history_discard_incomplete_raw(float* values, uint16_t slot_count);

// Fill only the history prefix older than raw coverage. Values in the covered
// raw region, including NAN gaps, always remain authoritative.
size_t ha_history_merge_older_statistics(float* raw_values,
                                         const float* statistics_values,
                                         uint16_t slot_count,
                                         uint16_t raw_coverage_start);

// Convert an exact millisecond bucket range to the whole-second timestamps
// required by Home Assistant's Recorder service. Multiplication stays in
// milliseconds so non-integral slot durations do not accumulate epoch-scale
// drift before the final conversion.
void ha_stats_request_window(uint32_t slot_ms, uint16_t slot_count,
                             uint64_t end_bucket,
                             uint64_t* start_sec, uint64_t* end_sec);

// Resample `points` onto the bucket grid.
//
// out[i] receives the value for bucket `end_bucket - (out_count - 1) + i`.
// `source_period_ms` is the Recorder period (five minutes or one hour). A
// period covers [start, start + source_period_ms), never subsequent missing
// periods. Select the last overlapping period in each bucket; if it ends
// before the bucket's right edge, leave that bucket unknown. A preceding
// period can cover the first bucket even if its start is outside the window.
//
// `points` may be in any order; entries outside the output range are ignored.
// Returns the number of buckets that received a finite value.
size_t ha_stats_resample(const HaStatPoint* points, size_t point_count,
                         uint64_t slot_ms, uint64_t source_period_ms,
                         uint64_t end_bucket,
                         float* out, size_t out_count);

// Merge resampled buckets into a ring buffer.
//
// The ring holds `live_count` locally collected samples in the slots ending at
// `head - 1`, which corresponds to bucket `newest_bucket`. History may only
// fill the slots preceding that live region — live data always wins, including
// samples that arrived while the request was in flight.
//
// `values[value_count - 1]` corresponds to bucket `end_bucket`, so a request
// whose grid drifted while it was in flight still lands on the right slots.
//
// Returns the new occupied-slot count (>= live_count). The region is only
// extended back to the oldest bucket that actually carries a value, so leading
// gaps do not widen an initially empty chart. Existing historical slots can be
// replaced with gaps, including when a later response is entirely unavailable.
size_t ha_stats_merge(float* ring, size_t slot_count, size_t head,
                      size_t live_count, uint64_t newest_bucket,
                      const float* values, size_t value_count,
                      uint64_t end_bucket, size_t existing_count = 0);
