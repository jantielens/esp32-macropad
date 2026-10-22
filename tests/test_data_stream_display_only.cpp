#include <gtest/gtest.h>

#include <map>
#include <string>
#include <sys/time.h>

#include "Arduino.h"
#include "data_stream.h"
#include "esp_heap_caps.h"
#include "pad_config.h"
#include "widgets/widget.h"
#if HAS_HA_HISTORY
#include "ha_stats.h"
#include "ha_stats_resample.h"
#endif

static unsigned long now_ms = 10000;
static uint64_t now_unix_ms = 1704067201000ULL;
static PadDataStreamSnapshot pad_snapshot{};
static std::map<std::string, std::string> binding_values;
static const char* history_entity = "";
static uint16_t raw_hours = 0;

#if HAS_HA_HISTORY
static bool fetch_busy = false;
static unsigned fetch_requests = 0;
static uint64_t fetch_end_bucket = 0;
static uint16_t fetch_raw_hours = 0;

void ha_stats_init() {}
bool ha_stats_deliver() { return false; }
bool ha_stats_busy() { return fetch_busy; }
uint32_t ha_stats_backoff_remaining_ms() { return 0; }
bool ha_stats_request(data_stream_handle_t, uint32_t, const char*, uint32_t,
                      uint16_t, uint64_t end_bucket, uint16_t hours) {
    fetch_busy = true;
    fetch_requests++;
    fetch_end_bucket = end_bucket;
    fetch_raw_hours = hours;
    return true;
}
#endif

extern "C" unsigned long millis() { return now_ms; }
extern "C" unsigned long micros() { return now_ms * 1000; }

extern "C" int gettimeofday(struct timeval* tv, void*) {
    tv->tv_sec = static_cast<time_t>(now_unix_ms / 1000);
    tv->tv_usec = static_cast<suseconds_t>((now_unix_ms % 1000) * 1000);
    return 0;
}

extern "C" bool pad_config_get_data_stream_snapshot(uint8_t page,
                                                     PadDataStreamSnapshot* out) {
    if (page != 0 || !out) return false;
    *out = pad_snapshot;
    return true;
}

extern "C" bool pad_binding_expand_bindings(const PadBinding*, uint8_t,
                                            const char*, char*, size_t) {
    return false;
}

extern "C" bool binding_template_resolve(const char* binding, char* out, size_t out_len) {
    const auto value = binding_values.find(binding);
    strlcpy(out, value == binding_values.end() ? "---" : value->second.c_str(), out_len);
    return true;
}

static bool get_test_stream_params(const WidgetConfig* config, uint8_t stream_index,
                                   uint32_t* window_secs, uint16_t* slot_count,
                                   const char** binding, const char** ha_entity,
                                   uint16_t* out_raw_hours) {
    if (stream_index != 0) return false;
    *window_secs = 4;
    *slot_count = 4;
    *binding = config->data_binding[0];
    *ha_entity = history_entity;
    *out_raw_hours = raw_hours;
    return true;
}

#if HAS_HA_HISTORY
class DataStreamHistory : public testing::Test {
protected:
    data_stream_handle_t handle = DATA_STREAM_INVALID;
    uint32_t uid = 0;

    void SetUp() override {
        pad_snapshot = {};
        data_stream_rebuild();
        data_stream_init();
        static WidgetType widget{};
        if (!widget.name) {
            widget.name = "history_test";
            widget.getStreamParams = get_test_stream_params;
            widget_register(&widget);
        }
        now_unix_ms = 1704067203000ULL;
        binding_values = {{"[test:reading]", "100"}};
        fetch_busy = false;
        fetch_requests = 0;
        history_entity = "sensor.test";
        raw_hours = 0;
        pad_snapshot.button_count = 1;
        strlcpy(pad_snapshot.widgets[0].type, widget.name,
                sizeof(pad_snapshot.widgets[0].type));
        strlcpy(pad_snapshot.widgets[0].data_binding[0], "[test:reading]",
                sizeof(pad_snapshot.widgets[0].data_binding[0]));
        data_stream_rebuild();
        handle = data_stream_find("[test:reading]", 4, 4, history_entity);
        ASSERT_NE(handle, DATA_STREAM_INVALID);
        uid = data_stream_uid(handle);
    }

    void TearDown() override {
        pad_snapshot = {};
        data_stream_rebuild();
        history_entity = "";
    }

    DataStreamSnapshot snapshot() {
        DataStreamSnapshot result{};
        EXPECT_TRUE(data_stream_get(handle, &result));
        return result;
    }

    float at(const DataStreamSnapshot& state, unsigned index) {
        return state.samples[(state.head + state.slot_count - state.count + index) %
                             state.slot_count];
    }
};

TEST_F(DataStreamHistory, RetryRepairsFullHistoryWithoutOverwritingLiveSamples) {
    data_stream_poll();
    ASSERT_EQ(fetch_requests, 1u);
    const uint64_t first_end = fetch_end_bucket;
    const float partial[] = {10, NAN, 41, NAN};
    ASSERT_TRUE(data_stream_apply_history(handle, uid, first_end, partial, 4, false));
    const auto before = snapshot();
    ASSERT_EQ(before.count, 4);
    EXPECT_TRUE(std::isnan(at(before, 1)));

    fetch_busy = false;
    data_stream_poll();
    ASSERT_EQ(fetch_requests, 2u);  // A full ring is not necessarily all live.
    now_unix_ms += 1000;
    binding_values["[test:reading]"] = "200";
    data_stream_poll();
    const float recovered[] = {11, 22, 43, 44};
    ASSERT_TRUE(data_stream_apply_history(handle, uid, first_end, recovered, 4));
    const auto after = snapshot();
    EXPECT_GT(after.rev, before.rev);
    ASSERT_EQ(after.count, 4);
    EXPECT_FLOAT_EQ(at(after, 0), 22);
    EXPECT_FLOAT_EQ(at(after, 1), 43);
    EXPECT_FLOAT_EQ(at(after, 2), 100);
    EXPECT_FLOAT_EQ(at(after, 3), 200);
    fetch_busy = false;
    data_stream_poll();
    EXPECT_EQ(fetch_requests, 2u);
}

TEST_F(DataStreamHistory, RawLimitParticipatesInIdentityAndWorkerRequest) {
    data_stream_poll();
    EXPECT_EQ(fetch_raw_hours, 0);
    raw_hours = 24;
    data_stream_rebuild();
    EXPECT_EQ(data_stream_find("[test:reading]", 4, 4, history_entity),
              DATA_STREAM_INVALID);
    const auto changed = data_stream_find("[test:reading]", 4, 4, history_entity, 24);
    ASSERT_NE(changed, DATA_STREAM_INVALID);
    EXPECT_NE(data_stream_uid(changed), uid);
    const float old_history[] = {1, 2, 3, 4};
    EXPECT_FALSE(data_stream_apply_history(handle, uid, fetch_end_bucket, old_history, 4));
    fetch_busy = false;
    data_stream_poll();
    EXPECT_EQ(fetch_raw_hours, 24);
}

TEST_F(DataStreamHistory, RetryUpdatesValuesAndGapsEvenWhenCountDoesNotChange) {
    data_stream_poll();
    const float partial[] = {10, 20, 30, 40};
    ASSERT_TRUE(data_stream_apply_history(handle, uid, fetch_end_bucket, partial, 4, false));
    const auto before = snapshot();
    const float recovered[] = {11, NAN, 33, 44};
    ASSERT_TRUE(data_stream_apply_history(handle, uid, fetch_end_bucket, recovered, 4));
    const auto after = snapshot();
    EXPECT_EQ(after.count, before.count);
    EXPECT_GT(after.rev, before.rev);
    EXPECT_FLOAT_EQ(at(after, 0), 11);
    EXPECT_TRUE(std::isnan(at(after, 1)));
    EXPECT_FLOAT_EQ(at(after, 2), 33);
    EXPECT_FLOAT_EQ(at(after, 3), 100);
}

TEST_F(DataStreamHistory, RetriesStopAtBudgetAndKeepUsablePartialHistory) {
    for (unsigned attempt = 1; attempt <= 3; attempt++) {
        fetch_busy = false;
        data_stream_poll();
        ASSERT_EQ(fetch_requests, attempt);
        const float partial[] = {NAN, NAN, 41, NAN};
        ASSERT_TRUE(data_stream_apply_history(handle, uid, fetch_end_bucket, partial, 4, false));
    }
    fetch_busy = false;
    data_stream_poll();
    data_stream_poll();
    EXPECT_EQ(fetch_requests, 3u);
    const auto state = snapshot();
    EXPECT_FLOAT_EQ(at(state, 0), 41);
    EXPECT_FLOAT_EQ(at(state, 1), 100);
}

TEST_F(DataStreamHistory, RetryClearsLeadingAndEntirelyUnavailableBackfill) {
    data_stream_poll();
    const float partial[] = {10, 20, 30, 40};
    ASSERT_TRUE(data_stream_apply_history(handle, uid, fetch_end_bucket, partial, 4, false));
    const float recovered[] = {NAN, NAN, 33, NAN};
    ASSERT_TRUE(data_stream_apply_history(handle, uid, fetch_end_bucket, recovered, 4, false));
    auto state = snapshot();
    ASSERT_EQ(state.count, 4);
    EXPECT_TRUE(std::isnan(at(state, 0)));
    EXPECT_TRUE(std::isnan(at(state, 1)));
    EXPECT_FLOAT_EQ(at(state, 2), 33);
    EXPECT_FLOAT_EQ(at(state, 3), 100);

    const float unavailable[] = {NAN, NAN, NAN, NAN};
    ASSERT_TRUE(data_stream_apply_history(handle, uid, fetch_end_bucket, unavailable, 4));
    state = snapshot();
    ASSERT_EQ(state.count, 4);
    for (unsigned i = 0; i < 3; i++) EXPECT_TRUE(std::isnan(at(state, i)));
    EXPECT_FLOAT_EQ(at(state, 3), 100);
}

TEST_F(DataStreamHistory, HistoryWithoutLiveDataAdvancesAsUnknownAndCanBeRepaired) {
    binding_values.clear();
    data_stream_poll();
    const uint64_t first_end = fetch_end_bucket;
    const float history[] = {10, 20, 30, 40};
    ASSERT_TRUE(data_stream_apply_history(handle, uid, first_end, history, 4, false));
    now_unix_ms += 1000;
    data_stream_poll();
    const auto advanced = snapshot();
    EXPECT_FLOAT_EQ(at(advanced, 0), 20);
    EXPECT_TRUE(std::isnan(at(advanced, 3)));
    binding_values["[test:reading]"] = "70";
    data_stream_poll();
    const float recovered[] = {11, 22, 33, 44};
    ASSERT_TRUE(data_stream_apply_history(handle, uid, first_end, recovered, 4));
    const auto result = snapshot();
    EXPECT_FLOAT_EQ(at(result, 0), 22);
    EXPECT_FLOAT_EQ(at(result, 2), 44);
    EXPECT_FLOAT_EQ(at(result, 3), 70);
}
#endif

#if !HAS_HA_HISTORY
TEST(DataStreamDisplayOnly, PollsNumericBindingFromRegisteredWidget) {
    static WidgetType test_widget{};
    test_widget.name = "test_stream";
    test_widget.getStreamParams = get_test_stream_params;
    widget_register(&test_widget);
    ASSERT_EQ(widget_find("test_stream"), &test_widget);

    pad_snapshot = PadDataStreamSnapshot{};
    pad_snapshot.button_count = 1;
    strlcpy(pad_snapshot.widgets[0].type, "test_stream",
            sizeof(pad_snapshot.widgets[0].type));
    strlcpy(pad_snapshot.widgets[0].data_binding[0], "[health:cpu]",
            sizeof(pad_snapshot.widgets[0].data_binding[0]));
    binding_values = {{"[health:cpu]", "10.25"}};

    data_stream_init();
    data_stream_rebuild();
    const data_stream_handle_t handle =
        data_stream_find("[health:cpu]", 4, 4, "");
    ASSERT_NE(handle, DATA_STREAM_INVALID);

    data_stream_poll();
    DataStreamSnapshot snapshot{};
    ASSERT_TRUE(data_stream_get(handle, &snapshot));
    ASSERT_EQ(snapshot.count, 1);
    EXPECT_FLOAT_EQ(snapshot.last_value, 10.25f);
    const uint32_t initial_revision = snapshot.rev;

    now_unix_ms += 1000;
    binding_values["[health:cpu]"] = "12.5";
    data_stream_poll();

    ASSERT_TRUE(data_stream_get(handle, &snapshot));
    ASSERT_EQ(snapshot.count, 2);
    EXPECT_FLOAT_EQ(snapshot.samples[0], 10.25f);
    EXPECT_FLOAT_EQ(snapshot.samples[1], 12.5f);
    EXPECT_GT(snapshot.rev, initial_revision);
}
#endif
