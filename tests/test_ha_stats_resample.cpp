// ============================================================================
// Unit tests for ha_stats_resample.cpp — timestamp parsing, resampling, merge
// ============================================================================
// Host-native: the unit under test is deliberately free of Arduino, LVGL and
// PSRAM dependencies so this needs no stubs.

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "ha_stats_resample.h"

// ---------------------------------------------------------------------------
// Minimal test harness (matches project pattern)
// ---------------------------------------------------------------------------
static int g_tests = 0;
static int g_passed = 0;

#define TEST(name) static void name()
#define RUN(name) do {                                       \
    g_tests++;                                               \
    printf("  %-50s ", #name);                               \
    name();                                                  \
    g_passed++;                                              \
    printf("PASS\n");                                        \
} while (0)

#define ASSERT_EQ(actual, expected) do {                     \
    if ((actual) != (expected)) {                             \
        printf("FAIL\n    %s:%d: %lld != %lld\n",            \
               __FILE__, __LINE__,                            \
               (long long)(actual), (long long)(expected));   \
        assert(false);                                       \
    }                                                        \
} while (0)

#define ASSERT_NEAR(actual, expected) do {                   \
    if (!(fabs((double)(actual) - (double)(expected)) < 1e-4)) {  \
        printf("FAIL\n    %s:%d: %f != %f\n",                \
               __FILE__, __LINE__,                            \
               (double)(actual), (double)(expected));         \
        assert(false);                                       \
    }                                                        \
} while (0)

#define ASSERT_NAN(actual) do {                              \
    if (std::isfinite((double)(actual))) {                    \
        printf("FAIL\n    %s:%d: expected gap, got %f\n",    \
               __FILE__, __LINE__, (double)(actual));         \
        assert(false);                                       \
    }                                                        \
} while (0)

// ---------------------------------------------------------------------------
// ha_stats_parse_iso8601
// ---------------------------------------------------------------------------

TEST(parse_epoch_reference) {
    // 1970-01-01T00:00:00Z is the epoch; 2024-01-01T00:00:00+00:00 is a known
    // fixed point also used as the project's "clock is valid" threshold.
    ASSERT_EQ(ha_stats_parse_iso8601("2024-01-01T00:00:00+00:00"), 1704067200ULL);
}

TEST(parse_accepts_z_suffix) {
    ASSERT_EQ(ha_stats_parse_iso8601("2024-01-01T00:00:00Z"), 1704067200ULL);
}

TEST(parse_accepts_fractional_seconds) {
    ASSERT_EQ(ha_stats_parse_iso8601("2024-01-01T00:00:00.123456+00:00"), 1704067200ULL);
}

TEST(parse_applies_positive_offset) {
    // 02:00 local at +02:00 is midnight UTC.
    ASSERT_EQ(ha_stats_parse_iso8601("2024-01-01T02:00:00+02:00"), 1704067200ULL);
}

TEST(parse_applies_negative_offset) {
    ASSERT_EQ(ha_stats_parse_iso8601("2023-12-31T19:00:00-05:00"), 1704067200ULL);
}

TEST(parse_handles_leap_day) {
    ASSERT_EQ(ha_stats_parse_iso8601("2024-02-29T00:00:00Z"), 1709164800ULL);
}

TEST(parse_rejects_garbage) {
    ASSERT_EQ(ha_stats_parse_iso8601("not-a-date"), 0ULL);
    ASSERT_EQ(ha_stats_parse_iso8601(""), 0ULL);
    ASSERT_EQ(ha_stats_parse_iso8601(nullptr), 0ULL);
    ASSERT_EQ(ha_stats_parse_iso8601("2024-13-01T00:00:00Z"), 0ULL);
}

TEST(request_window_preserves_fractional_second_slots) {
    const uint32_t slot_ms = 338823;  // 24 hours / 255 points
    const uint16_t slot_count = 255;
    const uint64_t end_bucket = 5000000;
    uint64_t start_sec = 0, end_sec = 0;

    ha_stats_request_window(slot_ms, slot_count, end_bucket,
                            &start_sec, &end_sec);

    const uint64_t exact_end_ms = (end_bucket + 1) * (uint64_t)slot_ms;
    ASSERT_EQ(end_sec, exact_end_ms / 1000ULL);
    ASSERT_EQ(start_sec,
              (exact_end_ms - (uint64_t)slot_count * slot_ms) / 1000ULL);

    // Truncating each slot first would move this epoch-sized bucket more than
    // 47 days backwards, which is the regression this test guards against.
    const uint64_t truncated_end = (end_bucket + 1) * (slot_ms / 1000ULL);
    assert(end_sec - truncated_end > 47ULL * 86400ULL);
}

TEST(request_window_supports_1024_slots) {
    const uint32_t slot_ms = 590625;  // 7 days / 1024 points
    const uint16_t slot_count = 1024;
    const uint64_t end_bucket = 3000000;
    uint64_t start_sec = 0;
    uint64_t end_sec = 0;

    ha_stats_request_window(slot_ms, slot_count, end_bucket,
                            &start_sec, &end_sec);

    ASSERT_EQ(end_sec - start_sec, 604800u);
}

// ---------------------------------------------------------------------------
// ha_stats_resample
// ---------------------------------------------------------------------------

// 5-minute buckets, so bucket id == start_sec / 300.
static const uint64_t SLOT_MS = 300000;

TEST(resample_aligns_points_to_buckets) {
    HaStatPoint pts[3] = {
        { 1000 * 300, 10.0f },
        { 1001 * 300, 20.0f },
        { 1002 * 300, 30.0f },
    };
    float out[4];
    size_t filled = ha_stats_resample(pts, 3, SLOT_MS, 1002, out, 4);
    ASSERT_EQ(filled, 3u);
    ASSERT_NAN(out[0]);          // bucket 999 — no point
    ASSERT_NEAR(out[1], 10.0f);
    ASSERT_NEAR(out[2], 20.0f);
    ASSERT_NEAR(out[3], 30.0f);
}

TEST(resample_supports_slot_index_above_255) {
    HaStatPoint point = { 1800 * 600, 42.0f };
    float out[1024];

    size_t filled = ha_stats_resample(&point, 1, 600000, 2000, out, 1024);

    ASSERT_EQ(filled, 1u);
    ASSERT_NEAR(out[823], 42.0f);
}

TEST(resample_leaves_missing_periods_as_gaps) {
    HaStatPoint pts[2] = {
        { 1000 * 300, 10.0f },
        { 1003 * 300, 40.0f },
    };
    float out[4];
    size_t filled = ha_stats_resample(pts, 2, SLOT_MS, 1003, out, 4);
    ASSERT_EQ(filled, 2u);
    ASSERT_NEAR(out[0], 10.0f);
    ASSERT_NAN(out[1]);
    ASSERT_NAN(out[2]);
    ASSERT_NEAR(out[3], 40.0f);
}

TEST(resample_ignores_points_outside_window) {
    HaStatPoint pts[3] = {
        { 900 * 300, 1.0f },     // Far older than the window
        { 1002 * 300, 20.0f },
        { 2000 * 300, 99.0f },   // In the future
    };
    float out[3];
    size_t filled = ha_stats_resample(pts, 3, SLOT_MS, 1002, out, 3);
    ASSERT_EQ(filled, 1u);
    ASSERT_NEAR(out[2], 20.0f);
}

TEST(resample_last_point_in_bucket_wins) {
    // A coarse slot grid can cover several Recorder periods. The live ingest
    // path also overwrites within a slot, so history must behave the same.
    HaStatPoint pts[3] = {
        { 3600, 1.0f },
        { 3900, 2.0f },
        { 4200, 3.0f },
    };
    float out[1];
    size_t filled = ha_stats_resample(pts, 3, 3600000, 1, out, 1);  // 1 h slots
    ASSERT_EQ(filled, 1u);
    ASSERT_NEAR(out[0], 3.0f);
}

TEST(resample_skips_non_finite_values) {
    HaStatPoint pts[2] = {
        { 1000 * 300, NAN },
        { 1001 * 300, 5.0f },
    };
    float out[2];
    size_t filled = ha_stats_resample(pts, 2, SLOT_MS, 1001, out, 2);
    ASSERT_EQ(filled, 1u);
    ASSERT_NAN(out[0]);
    ASSERT_NEAR(out[1], 5.0f);
}

TEST(resample_handles_empty_input) {
    float out[3] = { 1.0f, 2.0f, 3.0f };
    ASSERT_EQ(ha_stats_resample(nullptr, 0, SLOT_MS, 1000, out, 3), 0u);
    ASSERT_NAN(out[0]);
    ASSERT_NAN(out[1]);
    ASSERT_NAN(out[2]);
}

// ---------------------------------------------------------------------------
// ha_stats_merge
// ---------------------------------------------------------------------------
//
// Ring layout convention: logical position slot_count-1 is the newest slot and
// sits at ring[head-1]; live samples occupy the trailing `live_count` slots.

TEST(merge_fills_empty_ring) {
    float ring[4] = { 0, 0, 0, 0 };
    float vals[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    // head=0, no live data, newest bucket 100, values end at bucket 100.
    size_t count = ha_stats_merge(ring, 4, 0, 0, 100, vals, 4, 100);
    ASSERT_EQ(count, 4u);
    ASSERT_NEAR(ring[0], 1.0f);
    ASSERT_NEAR(ring[1], 2.0f);
    ASSERT_NEAR(ring[2], 3.0f);
    ASSERT_NEAR(ring[3], 4.0f);
}

TEST(merge_preserves_live_samples) {
    // Two live samples already occupy logical positions 2 and 3 (ring[0..1]
    // after head wrapped). History may only touch positions 0 and 1.
    float ring[4] = { 70.0f, 80.0f, 0, 0 };
    float vals[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    size_t count = ha_stats_merge(ring, 4, /*head*/2, /*live*/2, 100, vals, 4, 100);
    ASSERT_EQ(count, 4u);
    ASSERT_NEAR(ring[2], 1.0f);   // logical 0
    ASSERT_NEAR(ring[3], 2.0f);   // logical 1
    ASSERT_NEAR(ring[0], 70.0f);  // live, untouched
    ASSERT_NEAR(ring[1], 80.0f);  // live, untouched
}

TEST(merge_is_noop_when_live_covers_window) {
    float ring[3] = { 1.0f, 2.0f, 3.0f };
    float vals[3] = { 9.0f, 9.0f, 9.0f };
    size_t count = ha_stats_merge(ring, 3, 0, 3, 100, vals, 3, 100);
    ASSERT_EQ(count, 3u);
    ASSERT_NEAR(ring[0], 1.0f);
    ASSERT_NEAR(ring[1], 2.0f);
    ASSERT_NEAR(ring[2], 3.0f);
}

TEST(merge_shifts_when_grid_drifted_during_fetch) {
    // The response ends at bucket 99 but the ring advanced to 100 while the
    // request was in flight, so every value must land one slot older.
    float ring[4] = { 0, 0, 0, 0 };
    float vals[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    size_t count = ha_stats_merge(ring, 4, 0, 0, /*newest*/100, vals, 4, /*end*/99);
    ASSERT_EQ(count, 4u);
    ASSERT_NEAR(ring[0], 2.0f);   // bucket 97
    ASSERT_NEAR(ring[1], 3.0f);   // bucket 98
    ASSERT_NEAR(ring[2], 4.0f);   // bucket 99
    ASSERT_NAN(ring[3]);          // bucket 100 has no history yet
}

TEST(merge_trims_leading_gaps) {
    // Only the two newest buckets carry data; the valid region must not be
    // extended over the empty older ones.
    float ring[4] = { 0, 0, 0, 0 };
    float vals[4] = { NAN, NAN, 3.0f, 4.0f };
    size_t count = ha_stats_merge(ring, 4, 0, 0, 100, vals, 4, 100);
    ASSERT_EQ(count, 2u);
    ASSERT_NEAR(ring[2], 3.0f);
    ASSERT_NEAR(ring[3], 4.0f);
}

TEST(merge_keeps_interior_gaps) {
    float ring[4] = { 0, 0, 0, 0 };
    float vals[4] = { 1.0f, NAN, 3.0f, 4.0f };
    size_t count = ha_stats_merge(ring, 4, 0, 0, 100, vals, 4, 100);
    ASSERT_EQ(count, 4u);
    ASSERT_NEAR(ring[0], 1.0f);
    ASSERT_NAN(ring[1]);
    ASSERT_NEAR(ring[2], 3.0f);
    ASSERT_NEAR(ring[3], 4.0f);
}

TEST(merge_ignores_all_gap_response) {
    float ring[3] = { 0, 0, 0 };
    float vals[3] = { NAN, NAN, NAN };
    size_t count = ha_stats_merge(ring, 3, 0, 1, 100, vals, 3, 100);
    ASSERT_EQ(count, 1u);
}

TEST(merge_wraps_around_ring) {
    // head=3 on a 4-slot ring: logical 0 is ring[3], then it wraps to ring[0].
    float ring[4] = { 0, 0, 0, 0 };
    float vals[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    size_t count = ha_stats_merge(ring, 4, /*head*/3, /*live*/1, 100, vals, 4, 100);
    ASSERT_EQ(count, 4u);
    ASSERT_NEAR(ring[3], 1.0f);   // logical 0
    ASSERT_NEAR(ring[0], 2.0f);   // logical 1
    ASSERT_NEAR(ring[1], 3.0f);   // logical 2
    ASSERT_NEAR(ring[2], 0.0f);   // logical 3 is live — untouched
}

TEST(merge_ignores_short_response) {
    // Response covers fewer buckets than the ring; only the newest free slots
    // get filled and the region stops at the first covered bucket.
    float ring[4] = { 0, 0, 0, 0 };
    float vals[2] = { 3.0f, 4.0f };
    size_t count = ha_stats_merge(ring, 4, 0, 0, 100, vals, 2, 100);
    ASSERT_EQ(count, 2u);
    ASSERT_NEAR(ring[2], 3.0f);
    ASSERT_NEAR(ring[3], 4.0f);
}

TEST(merge_rejects_degenerate_input) {
    float ring[4] = { 0, 0, 0, 0 };
    float vals[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    ASSERT_EQ(ha_stats_merge(nullptr, 4, 0, 1, 100, vals, 4, 100), 1u);
    ASSERT_EQ(ha_stats_merge(ring, 0, 0, 0, 100, vals, 4, 100), 0u);
    ASSERT_EQ(ha_stats_merge(ring, 4, 0, 1, 100, nullptr, 0, 100), 1u);
    // Bucket grid not established yet (newest_bucket smaller than the ring).
    ASSERT_EQ(ha_stats_merge(ring, 4, 0, 1, 2, vals, 4, 2), 1u);
}

// ---------------------------------------------------------------------------

int main() {
    printf("\n=== ha_stats_resample tests ===\n\n");

    printf("ISO 8601 parsing:\n");
    RUN(parse_epoch_reference);
    RUN(parse_accepts_z_suffix);
    RUN(parse_accepts_fractional_seconds);
    RUN(parse_applies_positive_offset);
    RUN(parse_applies_negative_offset);
    RUN(parse_handles_leap_day);
    RUN(parse_rejects_garbage);

    printf("\nRequest window:\n");
    RUN(request_window_preserves_fractional_second_slots);
    RUN(request_window_supports_1024_slots);

    printf("\nResampling:\n");
    RUN(resample_aligns_points_to_buckets);
    RUN(resample_supports_slot_index_above_255);
    RUN(resample_leaves_missing_periods_as_gaps);
    RUN(resample_ignores_points_outside_window);
    RUN(resample_last_point_in_bucket_wins);
    RUN(resample_skips_non_finite_values);
    RUN(resample_handles_empty_input);

    printf("\nRing merge:\n");
    RUN(merge_fills_empty_ring);
    RUN(merge_preserves_live_samples);
    RUN(merge_is_noop_when_live_covers_window);
    RUN(merge_shifts_when_grid_drifted_during_fetch);
    RUN(merge_trims_leading_gaps);
    RUN(merge_keeps_interior_gaps);
    RUN(merge_ignores_all_gap_response);
    RUN(merge_wraps_around_ring);
    RUN(merge_ignores_short_response);
    RUN(merge_rejects_degenerate_input);

    printf("\n%d/%d tests passed\n\n", g_passed, g_tests);
    return (g_passed == g_tests) ? 0 : 1;
}
