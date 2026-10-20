#include <gtest/gtest.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <lvgl.h>
#include "board_config.h"
#include "log_manager.h"
#define HAS_MCP 0
#include "data_stream.h"
#include "binding_template.h"

static unsigned long now_ms = 10000;
extern "C" unsigned long millis() { return now_ms; }
extern "C" unsigned long micros() { return now_ms * 1000; }

static float samples[3][4];
static DataStreamSnapshot streams[3];
static std::map<std::string, std::string> bindings;
static unsigned binding_resolutions;

bool binding_template_has_bindings(const char* text) { return text && text[0] == '['; }
bool binding_template_resolve(const char* text, char* out, size_t size) {
    ++binding_resolutions;
    auto found = bindings.find(text);
    strlcpy(out, found == bindings.end() ? text : found->second.c_str(), size);
    return true;
}
data_stream_handle_t data_stream_find(const char* binding, uint32_t, uint16_t, const char*, uint8_t) {
    if (strcmp(binding, "[test:stream3]") == 0) return 2;
    return strcmp(binding, "[test:stream2]") == 0 ? 1 : 0;
}
bool data_stream_get(data_stream_handle_t handle, DataStreamSnapshot* out) {
    if (handle < 0 || handle >= 3) return false;
    *out = streams[handle];
    return true;
}

#include "widgets/widget.cpp"
#include "widgets/sparkline_widget.cpp"

class SparklineWidget : public testing::Test {
protected:
    lv_display_t* display = nullptr;
    lv_obj_t* tile = nullptr;
    WidgetConfig config{};
    WidgetState state{};
    const WidgetType* type = nullptr;
    std::vector<uint8_t> buffer;
    unsigned invalidations = 0;
    unsigned flushes = 0;

    SparklineState* chart() { return reinterpret_cast<SparklineState*>(state.data); }

    void SetUp() override {
        static bool initialized = false;
        if (!initialized) { lv_init(); initialized = true; }
        now_ms = 10000;
        bindings = {{"[test:min]", "0"}, {"[test:max]", "100"},
                    {"[test:color]", "#123456"}, {"[test:label]", "value"},
                    {"[test:reference]", "#ABCDEF"}};
        binding_resolutions = 0;
        for (unsigned i = 0; i < 3; ++i) {
            for (unsigned j = 0; j < 4; ++j) samples[i][j] = 20 + i * 10 + j * 10;
            streams[i] = {samples[i], 4, 0, 4, 1, samples[i][0], samples[i][3], samples[i][3]};
        }
        display = lv_display_create(600, 400);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
        buffer.resize(600 * 20 * 2);
        lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size(), LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_user_data(display, this);
        lv_display_set_flush_cb(display, [](lv_display_t* disp, const lv_area_t*, uint8_t*) {
            ++static_cast<SparklineWidget*>(lv_display_get_user_data(disp))->flushes;
            lv_display_flush_ready(disp);
        });
        lv_display_add_event_cb(display, [](lv_event_t* event) {
            ++static_cast<SparklineWidget*>(lv_event_get_user_data(event))->invalidations;
        }, LV_EVENT_INVALIDATE_AREA, this);
        tile = lv_obj_create(lv_display_get_screen_active(display));
        lv_obj_set_size(tile, 580, 380);
        JsonDocument document;
        JsonObject fields = document.to<JsonObject>();
        fields["widget_sparkline_slots"] = 4;
        fields["widget_sparkline_min"] = "[test:min]";
        fields["widget_sparkline_max"] = "[test:max]";
        fields["widget_sparkline_line_color"] = "[test:color]";
        fields["widget_sparkline_current_label"] = "[test:label]";
        fields["widget_sparkline_current_label_2"] = "44.3 %";
        fields["widget_sparkline_current_label_3"] = "1007.1 hPa";
        fields["widget_sparkline_current_dot"] = 5;
        fields["widget_sparkline_marker_size_min"] = 5;
        fields["widget_sparkline_marker_size_max"] = 5;
        fields["widget_sparkline_ref_1_y"] = 35;
        fields["widget_sparkline_ref_1_color"] = "[test:reference]";
        fields["widget_sparkline_smooth"] = 2;
        type = widget_find("sparkline");
        ASSERT_NE(type, nullptr);
        type->parseConfig(fields, config.data);
        strlcpy(config.data_binding[0], "[test:stream]", sizeof(config.data_binding[0]));
        strlcpy(config.data_binding[1], "[test:stream2]", sizeof(config.data_binding[1]));
        strlcpy(config.data_binding[2], "[test:stream3]", sizeof(config.data_binding[2]));
        ScreenButtonConfig button{};
        PadRect rect{};
        rect.w = 580;
        rect.h = 380;
        UIScaleInfo scale{};
        scale.font_small = LV_FONT_DEFAULT;
        type->createUI(tile, &config, &button, &rect, &scale, nullptr, nullptr, &state);
        ASSERT_NE(chart()->lines[0], nullptr);
        ASSERT_NE(chart()->lines[1], nullptr);
        ASSERT_NE(chart()->lines[2], nullptr);
        update();
        settle();
    }

    void TearDown() override {
        if (type) type->destroyUI(&state);
        lv_display_delete(display);
    }

    void update() { type->update(tile, &config, &state, "ignored"); }
    void tick() { type->tick(tile, &config, &state); }
    void settle() {
        lv_refr_now(display);
        invalidations = flushes = 0;
    }
};

TEST_F(SparklineWidget, CurrentDotsAlignWithLineEndpointsOnFirstFrame) {
    for (uint8_t i = 0; i < chart()->line_count; ++i) {
        SCOPED_TRACE(testing::Message() << "line " << unsigned(i));
        lv_obj_t* line = chart()->lines[i];
        lv_obj_t* dot = chart()->dot_current[i];
        ASSERT_NE(dot, nullptr);
        ASSERT_FALSE(lv_obj_has_flag(dot, LV_OBJ_FLAG_HIDDEN));
        const uint32_t count = lv_line_get_point_count(line);
        ASSERT_GT(count, 0u);
        lv_area_t line_bounds;
        lv_area_t dot_bounds;
        lv_obj_get_coords(line, &line_bounds);
        lv_obj_get_coords(dot, &dot_bounds);
        const lv_point_precise_t& endpoint = chart()->points[i][count - 1];
        EXPECT_EQ(dot_bounds.x1 + lv_obj_get_width(dot) / 2, line_bounds.x1 + endpoint.x);
        EXPECT_EQ(dot_bounds.y1 + lv_obj_get_height(dot) / 2, line_bounds.y1 + endpoint.y);
    }
}

TEST_F(SparklineWidget, CurrentLabelsStayInsideChartAndSeparatedAtScaleEdges) {
    const float endpoints[][3] = {
        {0, 100, 0},
        {100, 100, 0},
        {0, 0, 0},
        {100, 100, 100},
    };
    for (const auto& values : endpoints) {
        SCOPED_TRACE(testing::Message() << values[0] << ", " << values[1] << ", " << values[2]);
        for (unsigned i = 0; i < 3; ++i) {
            for (float& sample : samples[i]) sample = values[i];
            streams[i].auto_min = streams[i].auto_max = streams[i].last_value = values[i];
            ++streams[i].rev;
        }
        update();
        lv_obj_update_layout(tile);

        lv_area_t chart_bounds;
        lv_obj_get_coords(lv_obj_get_parent(chart()->lines[0]), &chart_bounds);
        lv_area_t button_bounds;
        lv_obj_get_content_coords(tile, &button_bounds);
        std::vector<lv_area_t> labels;
        for (lv_obj_t* label : chart()->lbl_current) {
            ASSERT_NE(label, nullptr);
            ASSERT_FALSE(lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN));
            lv_area_t bounds;
            lv_obj_get_coords(label, &bounds);
            EXPECT_GE(bounds.y1, chart_bounds.y1);
            EXPECT_LE(bounds.y2, chart_bounds.y2);
            EXPECT_GE(bounds.y1, button_bounds.y1);
            EXPECT_LE(bounds.y2, button_bounds.y2);
            labels.push_back(bounds);
        }
        std::sort(labels.begin(), labels.end(), [](const lv_area_t& a, const lv_area_t& b) {
            return a.y1 < b.y1;
        });
        for (size_t i = 1; i < labels.size(); ++i) {
            EXPECT_GE(labels[i].y1 - labels[i - 1].y2 - 1, 3);
        }
    }
}

TEST_F(SparklineWidget, UnchangedOutputDoesNotInvalidateOrFlush) {
    update();
    EXPECT_EQ(invalidations, 0u);
    const unsigned resolved = binding_resolutions;
    tick();
    EXPECT_EQ(binding_resolutions, resolved);
    now_ms += 1001;
    tick();
    EXPECT_EQ(invalidations, 0u);
    lv_refr_now(display);
    EXPECT_EQ(flushes, 0u);
}

TEST_F(SparklineWidget, StreamChangesAndHistoryCountChangesStillRedraw) {
    ++streams[0].rev;
    tick();
    EXPECT_EQ(invalidations, 0u);
    const auto old_y = chart()->points[0][2].y;
    samples[0][2] = 80;
    ++streams[0].rev;
    tick();
    EXPECT_NE(chart()->points[0][2].y, old_y);
    EXPECT_GT(invalidations, 0u);
    settle();
    samples[0][2] = NAN;
    ++streams[0].rev;
    tick();
    EXPECT_EQ(lv_line_get_point_count(chart()->lines[0]), 3u);
    EXPECT_GT(invalidations, 0u);
}

TEST_F(SparklineWidget, ReferenceVisibilityAndColorFollowDynamicScaleAndBindings) {
    ASSERT_NE(chart()->ref_line_objs[0], nullptr);
    EXPECT_EQ(lv_line_get_point_count(chart()->ref_line_objs[0]), 2u);
    const auto old_y = chart()->ref_pts[0].y;
    bindings["[test:max]"] = "200";
    update();
    EXPECT_NE(chart()->ref_pts[0].y, old_y);
    bindings["[test:min]"] = "40";
    update();
    EXPECT_TRUE(lv_obj_has_flag(chart()->ref_line_objs[0], LV_OBJ_FLAG_HIDDEN));
    EXPECT_EQ(lv_line_get_point_count(chart()->ref_line_objs[0]), 0u);
    bindings["[test:min]"] = "0";
    bindings["[test:reference]"] = "#010203";
    update();
    EXPECT_FALSE(lv_obj_has_flag(chart()->ref_line_objs[0], LV_OBJ_FLAG_HIDDEN));
    EXPECT_EQ(lv_line_get_point_count(chart()->ref_line_objs[0]), 2u);
    EXPECT_TRUE(lv_color_eq(lv_obj_get_style_line_color(chart()->ref_line_objs[0], LV_PART_MAIN),
                            lv_color_hex(0x010203)));
}

TEST_F(SparklineWidget, DynamicScaleColorAndLabelsStillUpdateWithoutStreamChanges) {
    const auto old_y = chart()->points[0][3].y;
    bindings["[test:max]"] = "200";
    update();
    EXPECT_NE(chart()->points[0][3].y, old_y);
    EXPECT_GT(invalidations, 0u);
    settle();
    bindings["[test:color]"] = "#FEDCBA";
    update();
    EXPECT_TRUE(lv_color_eq(lv_obj_get_style_line_color(chart()->lines[0], LV_PART_MAIN),
                            lv_color_hex(0xFEDCBA)));
    EXPECT_TRUE(lv_color_eq(lv_obj_get_style_bg_color(chart()->dot_current[0], LV_PART_MAIN),
                            lv_color_hex(0xFEDCBA)));
    EXPECT_GT(invalidations, 0u);
    settle();
    bindings["[test:label]"] = "new value";
    now_ms += 1001;
    tick();
    EXPECT_STREQ(lv_label_get_text(chart()->lbl_current[0]), "new value");
    EXPECT_GT(invalidations, 0u);
}
