#include <gtest/gtest.h>

#include <map>
#include <string>
#include <sys/time.h>

#include "Arduino.h"
#include "data_stream.h"
#include "esp_heap_caps.h"
#include "pad_config.h"
#include "widgets/widget.h"

static unsigned long now_ms = 10000;
static uint64_t now_unix_ms = 1704067201000ULL;
static PadDataStreamSnapshot pad_snapshot{};
static std::map<std::string, std::string> binding_values;

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
                                   uint8_t* ha_stat) {
    if (stream_index != 0) return false;
    *window_secs = 4;
    *slot_count = 4;
    *binding = config->data_binding[0];
    *ha_entity = "";
    *ha_stat = 0;
    return true;
}

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
        data_stream_find("[health:cpu]", 4, 4, "", 0);
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
