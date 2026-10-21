#include <gtest/gtest.h>

#include "ha_history_decoder.h"
#include "ha_stats_resample.h"
#include "bounded_psram_json_allocator.h"

#include <array>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>

namespace {
struct CollectedRecords {
    std::vector<std::string> records;
};

bool collect_record(const char* json, size_t size, void* context) {
    auto& collected = *static_cast<CollectedRecords*>(context);
    collected.records.emplace_back(json, size);
    return true;
}

bool reject_record(const char*, size_t, void*) {
    return false;
}

bool parse_record(const char* json, size_t size, void* context) {
    auto& document = *static_cast<JsonDocument*>(context);
    uint64_t timestamp = 0;
    float value = NAN;
    return ha_history_parse_record(json, size, document, &timestamp, &value);
}
}

TEST(HaHistoryDecoder, HandlesFragmentedNestedArraysAndEscapedStrings) {
    const std::string response =
        "[[{\"entity_id\":\"sensor.test\",\"state\":\"0\","
        "\"last_changed\":\"2026-10-21T12:00:00.123456+00:00\","
        "\"attributes\":{\"name\":\"brace } and quote \\\"\"}},"
        "{\"state\":\"-4.5\",\"last_changed\":\"2026-10-21T12:00:01Z\"}]]";
    HaHistoryDecoder decoder;
    CollectedRecords records;
    for (char byte : response) {
        ASSERT_TRUE(decoder.feed(reinterpret_cast<const uint8_t*>(&byte), 1,
                                 collect_record, &records));
    }
    ASSERT_TRUE(decoder.finish());
    ASSERT_EQ(records.records.size(), 2u);
    EXPECT_EQ(decoder.records_seen(), 2u);
    EXPECT_EQ(decoder.peak_record_bytes(), records.records[0].size());

    JsonDocument document;
    uint64_t timestamp = 0;
    float value = NAN;
    ASSERT_TRUE(ha_history_parse_record(records.records[0].data(),
                                        records.records[0].size(), document,
                                        &timestamp, &value));
    EXPECT_EQ(timestamp, 1792584000123ULL);
    EXPECT_FLOAT_EQ(value, 0.0f);
    ASSERT_TRUE(ha_history_parse_record(records.records[1].data(),
                                        records.records[1].size(), document,
                                        &timestamp, &value));
    EXPECT_FLOAT_EQ(value, -4.5f);
}

TEST(HaHistoryDecoder, AcceptsEmptyHistoryShapes) {
    for (const char* response : {"[]", "[[]]"}) {
        HaHistoryDecoder decoder;
        CollectedRecords records;
        ASSERT_TRUE(decoder.feed(reinterpret_cast<const uint8_t*>(response),
                                 strlen(response), collect_record, &records));
        EXPECT_TRUE(decoder.finish());
        EXPECT_TRUE(records.records.empty());
    }
}

TEST(HaHistoryDecoder, RejectsWrongShapeExtraEntityArrayAndTrailingData) {
    for (const char* response : {"{}", "[{}]", "[[{}],[{}]]", "[[{}]]x", "[[{\"x\":1},]]"}) {
        HaHistoryDecoder decoder;
        CollectedRecords records;
        EXPECT_FALSE(decoder.feed(reinterpret_cast<const uint8_t*>(response),
                                  strlen(response), collect_record, &records))
            << response;
        EXPECT_NE(decoder.error(), HaHistoryDecoder::Error::None);
    }
}

TEST(HaHistoryDecoder, RejectsIncompleteJsonAndInvalidRecord) {
    for (const char* response : {"", "[", "[[", "[[{\"state\":\"2\"}]"}) {
        HaHistoryDecoder decoder;
        CollectedRecords records;
        ASSERT_TRUE(decoder.feed(reinterpret_cast<const uint8_t*>(response),
                                 strlen(response), collect_record, &records));
        EXPECT_FALSE(decoder.finish()) << response;
        EXPECT_EQ(decoder.error(), HaHistoryDecoder::Error::Incomplete);
    }

    HaHistoryDecoder decoder;
    JsonDocument document;
    const char malformed[] = "[[{bad}]]";
    EXPECT_FALSE(decoder.feed(reinterpret_cast<const uint8_t*>(malformed),
                              sizeof(malformed) - 1, parse_record, &document));
    EXPECT_EQ(decoder.error(), HaHistoryDecoder::Error::CallbackFailed);
}

TEST(HaHistoryDecoder, EnforcesRecordAndBodyBudgets) {
    std::string oversized = "[[{" + std::string(HaHistoryDecoder::MAX_RECORD_BYTES, 'x') + "}]]";
    HaHistoryDecoder record_decoder;
    CollectedRecords records;
    EXPECT_FALSE(record_decoder.feed(reinterpret_cast<const uint8_t*>(oversized.data()),
                                     oversized.size(), collect_record, &records));
    EXPECT_EQ(record_decoder.error(), HaHistoryDecoder::Error::RecordTooLarge);

    std::vector<uint8_t> body(HaHistoryDecoder::MAX_BODY_BYTES + 1, ' ');
    HaHistoryDecoder body_decoder;
    EXPECT_FALSE(body_decoder.feed(body.data(), body.size(), collect_record, &records));
    EXPECT_EQ(body_decoder.error(), HaHistoryDecoder::Error::BodyLimit);

    HaHistoryDecoder callback_decoder;
    const char response[] = "[[{}]]";
    EXPECT_FALSE(callback_decoder.feed(reinterpret_cast<const uint8_t*>(response),
                                       sizeof(response) - 1, reject_record, nullptr));
    EXPECT_EQ(callback_decoder.error(), HaHistoryDecoder::Error::CallbackFailed);
}

TEST(HaHistoryDecoder, StrictlyParsesNumericAndUnavailableStates) {
    JsonDocument document;
    uint64_t timestamp = 0;
    float value = 0;
    const char numeric[] = "{\"state\":\"-2.25\",\"last_updated\":\"2026-10-21T12:00:00Z\"}";
    ASSERT_TRUE(ha_history_parse_record(numeric, sizeof(numeric) - 1, document,
                                        &timestamp, &value));
    EXPECT_FLOAT_EQ(value, -2.25f);

    const char unavailable[] = "{\"state\":\"unavailable\",\"last_changed\":\"2026-10-21T12:00:00Z\"}";
    ASSERT_TRUE(ha_history_parse_record(unavailable, sizeof(unavailable) - 1, document,
                                        &timestamp, &value));
    EXPECT_TRUE(std::isnan(value));

    const char partial[] = "{\"state\":\"41garbage\",\"last_changed\":\"2026-10-21T12:00:00Z\"}";
    ASSERT_TRUE(ha_history_parse_record(partial, sizeof(partial) - 1, document,
                                        &timestamp, &value));
    EXPECT_TRUE(std::isnan(value));

    const char malformed_time[] = "{\"state\":\"2\",\"last_changed\":\"2026-02-30T12:00:00Z\"}";
    EXPECT_FALSE(ha_history_parse_record(malformed_time, sizeof(malformed_time) - 1,
                                         document, &timestamp, &value));
}

TEST(HaHistoryDecoder, ThirtyDayStatisticsFitBoundedWorkspaceAndStitchWithRetainedRaw) {
    constexpr uint32_t slot_ms = 2531250;  // Thirty days / 1024 points.
    constexpr uint16_t count = 1024;
    const uint64_t end_bucket = 1792627200000ULL / slot_ms;
    const uint64_t start_ms = (end_bucket + 1 - count) * slot_ms;
    for (unsigned retained_days : {0u, 10u}) {
        SCOPED_TRACE(retained_days);
        std::array<float, count> raw{}, statistics{};
        HaRawHistorySampler sampler;
        ASSERT_TRUE(ha_raw_history_sampler_init(
            &sampler, raw.data(), count, slot_ms, start_ms, start_ms + 30ULL * 86400000));
        if (retained_days) {
            ASSERT_TRUE(ha_raw_history_sampler_add(
                &sampler, start_ms + 20ULL * 86400000, 41));
            ASSERT_TRUE(ha_raw_history_sampler_add(
                &sampler, start_ms + 25ULL * 86400000, NAN));
        }

        const uint16_t cutoff = ha_raw_history_sampler_finish(&sampler);
        const auto window = ha_history_statistics_window(slot_ms, count, end_bucket, cutoff);
        std::string response = "{\"service_response\":{\"statistics\":{\"sensor.test\":[";
        for (uint64_t sec = window.start_sec; sec < window.end_sec; sec += window.period_sec) {
            const time_t time = static_cast<time_t>(sec);
            struct tm utc{};
            gmtime_r(&time, &utc);
            char timestamp[32];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S+00:00", &utc);
            if (sec != window.start_sec) response += ',';
            response += "{\"start\":\"" + std::string(timestamp) + "\",\"mean\":12.5}";
        }
        response += "]}}}";
        BoundedPsramJsonAllocator allocator(HA_STATISTICS_JSON_BYTES);
        JsonDocument document(&allocator);
        const auto error = deserializeJson(document, response);
        ASSERT_FALSE(error) << error.c_str() << ", peak " << allocator.peak;
        const auto periods = document["service_response"]["statistics"]["sensor.test"]
                                 .as<JsonArrayConst>();
        ASSERT_GT(periods.size(), 320u);
        ASSERT_LE(periods.size(), HA_STATISTICS_MAX_POINTS);
        ASSERT_LE(allocator.peak, HA_STATISTICS_JSON_BYTES);
        std::array<HaStatPoint, HA_STATISTICS_MAX_POINTS> points{};
        size_t n = 0;
        for (JsonObjectConst period : periods) {
            points[n++] = {ha_stats_parse_iso8601(period["start"]),
                           period["mean"].as<float>()};
        }
        ASSERT_GT(ha_stats_resample(points.data(), n, slot_ms,
                                    window.period_sec * 1000ULL, end_bucket,
                                    statistics.data(), count), 0u);
        ha_history_merge_older_statistics(raw.data(), statistics.data(), count, cutoff);
        for (uint16_t i = 0; i < cutoff; i++) EXPECT_FLOAT_EQ(raw[i], 12.5f) << i;
        if (retained_days) {
            EXPECT_FLOAT_EQ(raw[cutoff], 41);
            EXPECT_FLOAT_EQ(raw[800], 41);
            EXPECT_TRUE(std::isnan(raw[900]));
        }
    }
}

TEST(HaHistoryDecoder, RawHourLimitBoundsRequestAndInitialStateCoverage) {
    constexpr uint32_t slot_ms = 2531250;
    constexpr uint16_t count = 1024;
    const uint64_t end_bucket = 1792627200000ULL / slot_ms;
    const uint64_t end_ms = (end_bucket + 1) * slot_ms;
    const uint64_t start_ms = end_ms - 30ULL * 86400000;
    EXPECT_EQ(ha_history_raw_start_ms(slot_ms, count, end_bucket, 0), start_ms);
    EXPECT_EQ(ha_history_raw_start_ms(slot_ms, count, end_bucket, 720), start_ms);
    EXPECT_EQ(ha_history_raw_start_ms(1000, 4, end_bucket, 24),
              (end_bucket + 1 - 4) * 1000);

    for (uint16_t hours : {1, 24, 72}) {
        SCOPED_TRACE(hours);
        const uint64_t raw_start = ha_history_raw_start_ms(slot_ms, count, end_bucket, hours);
        ASSERT_EQ(raw_start, end_ms - hours * 3600000ULL);
        std::array<float, count> raw{}, statistics{};
        HaRawHistorySampler sampler;
        ASSERT_TRUE(ha_raw_history_sampler_init(
            &sampler, raw.data(), count, slot_ms, start_ms, end_ms, raw_start));
        // Recorder includes the initial state with its original change time.
        ASSERT_TRUE(ha_raw_history_sampler_add(&sampler, start_ms - 86400000, 41));
        ASSERT_TRUE(ha_raw_history_sampler_add(&sampler, end_ms - 1000, NAN));
        const uint16_t cutoff = ha_raw_history_sampler_finish(&sampler);
        ASSERT_EQ(cutoff, (raw_start - start_ms) / slot_ms);
        for (uint16_t i = 0; i < cutoff; i++) EXPECT_TRUE(std::isnan(raw[i]));
        const auto window = ha_history_statistics_window(slot_ms, count, end_bucket, cutoff);
        EXPECT_EQ(window.end_sec, (start_ms + cutoff * (uint64_t)slot_ms + 999) / 1000);
        statistics.fill(12);
        ha_history_merge_older_statistics(raw.data(), statistics.data(), count, cutoff);
        EXPECT_FLOAT_EQ(raw[cutoff - 1], 12);
        if (cutoff < count - 1) {
            EXPECT_FLOAT_EQ(raw[cutoff], 41);
        }
        EXPECT_TRUE(std::isnan(raw.back()));
    }
}

TEST(HaHistoryDecoder, CappedRawUsesActualRetentionAndEmptyHistoryFallsBackFully) {
    constexpr uint32_t slot_ms = 3600000;
    constexpr uint16_t count = 72;
    const uint64_t end_bucket = 500000;
    const uint64_t end_ms = (end_bucket + 1) * slot_ms;
    const uint64_t start_ms = end_ms - count * (uint64_t)slot_ms;
    const uint64_t raw_start = ha_history_raw_start_ms(slot_ms, count, end_bucket, 24);
    std::array<float, count> values{};
    HaRawHistorySampler sampler;
    ASSERT_TRUE(ha_raw_history_sampler_init(
        &sampler, values.data(), count, slot_ms, start_ms, end_ms, raw_start));
    EXPECT_EQ(ha_raw_history_sampler_finish(&sampler), count);
    ASSERT_TRUE(ha_raw_history_sampler_add(&sampler, end_ms - 6ULL * slot_ms, 42));
    const auto cutoff = ha_raw_history_sampler_finish(&sampler);
    EXPECT_EQ(cutoff, 66);
    const auto window = ha_history_statistics_window(slot_ms, count, end_bucket, cutoff);
    EXPECT_EQ(window.end_sec, (end_ms - 6ULL * slot_ms) / 1000);
}
