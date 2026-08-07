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

// Parse a Recorder period start timestamp into unix seconds.
//
// Home Assistant serializes these as ISO 8601 — "2026-07-21T10:05:00+00:00",
// a trailing "Z", and fractional seconds are all accepted, and a numeric UTC
// offset is applied. Returns 0 when the input cannot be parsed.
uint64_t ha_stats_parse_iso8601(const char* s);

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
// Buckets with no covering period are left as NAN. When several periods land
// in one bucket the last one wins, matching the live ingest path (which also
// overwrites within a slot rather than averaging).
//
// `points` may be in any order; entries outside the output range are ignored.
// Returns the number of buckets that received a finite value.
size_t ha_stats_resample(const HaStatPoint* points, size_t point_count,
                         uint64_t slot_ms, uint64_t end_bucket,
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
// Returns the new valid-sample count (>= live_count). The region is only
// extended back to the oldest bucket that actually carries a value, so leading
// gaps do not widen the chart with empty space.
size_t ha_stats_merge(float* ring, size_t slot_count, size_t head,
                      size_t live_count, uint64_t newest_bucket,
                      const float* values, size_t value_count,
                      uint64_t end_bucket);
