#include <gtest/gtest.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <lvgl.h>
#include "board_config.h"
#include "log_manager.h"
#define HAS_MCP 1
#include "data_stream.h"
#include "binding_template.h"
#include "button_content_lvgl.h"

static unsigned long now_ms = 10000;
extern "C" unsigned long millis() { return now_ms; }
extern "C" unsigned long micros() { return now_ms * 1000; }

static float samples[3][4];
static DataStreamSnapshot streams[3];
static std::map<std::string, std::string> bindings;
static unsigned binding_resolutions;
static bool streams_available = true;

bool binding_template_has_bindings(const char* text) { return text && text[0] == '['; }
bool binding_template_resolve(const char* text, char* out, size_t size) {
    ++binding_resolutions;
    auto found = bindings.find(text);
    strlcpy(out, found == bindings.end() ? text : found->second.c_str(), size);
    return true;
}
bool binding_template_resolve_single_token(const char* text, char* out, size_t size) {
    if (!text || text[0] != '[' || text[strlen(text) - 1] != ']') return false;
    return binding_template_resolve(text, out, size);
}
data_stream_handle_t data_stream_find(const char* binding, uint32_t, uint16_t, const char*, uint16_t) {
    if (!streams_available) return DATA_STREAM_INVALID;
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
#undef TAG
static unsigned gauge_trig_calls = 0;
static float gauge_count_sin(float angle) { ++gauge_trig_calls; return sinf(angle); }
static float gauge_count_cos(float angle) { ++gauge_trig_calls; return cosf(angle); }
#define sinf gauge_count_sin
#define cosf gauge_count_cos
#include "widgets/gauge_widget.cpp"
#undef sinf
#undef cosf
#undef TAG
#include "widgets/bar_chart_widget.cpp"
#undef TAG
#include "widgets/table_widget.cpp"
#undef TAG

static unsigned other_schema_calls = 0;
static void describe_other_widget(JsonObject& out) {
    ++other_schema_calls;
    out["test"] = true;
}

TEST(SparklineSchema, FocusedLookupMatchesFullRegistry) {
    static WidgetType other = {};
    other.name = "other";
    other.describeSchema = describe_other_widget;
    widget_register(&other);
    JsonDocument full_doc;
    JsonArray full = full_doc.to<JsonArray>();
    ASSERT_TRUE(widget_schema_emit(full));
    ASSERT_EQ(full.size(), widget_count());
    ASSERT_EQ(other_schema_calls, 1u);
    other_schema_calls = 0;
    JsonDocument focused_doc;
    JsonArray focused = focused_doc["widgets"].to<JsonArray>();
    ASSERT_TRUE(widget_schema_emit(focused, "sparkline"));
    ASSERT_EQ(focused.size(), 1u);
    ASSERT_STREQ(focused[0]["type"], "sparkline");
    EXPECT_EQ(other_schema_calls, 0u);
    std::string full_json;
    std::string focused_json;
    serializeJson(full[0], full_json);
    serializeJson(focused[0], focused_json);
    EXPECT_EQ(focused_json, full_json);
    EXPECT_FALSE(focused[0]["history_note"].isNull());

    JsonDocument missing_doc;
    JsonArray missing = missing_doc.to<JsonArray>();
    EXPECT_FALSE(widget_schema_emit(missing, "unknown"));
    EXPECT_FALSE(widget_schema_emit(missing, ""));
    EXPECT_EQ(missing.size(), 0u);
}

class SparklineWidget : public testing::Test {
protected:
    lv_display_t* display = nullptr;
    lv_obj_t* tile = nullptr;
    WidgetConfig config{};
    WidgetState state{};
    const WidgetType* type = nullptr;
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> framebuffer;
    unsigned invalidations = 0;
    unsigned flushes = 0;

    SparklineState* chart() { return reinterpret_cast<SparklineState*>(state.data); }
    virtual bool initial_streams_available() const { return true; }

    void SetUp() override {
        static bool initialized = false;
        if (!initialized) { lv_init(); initialized = true; }
        now_ms = 10000;
        streams_available = initial_streams_available();
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
        framebuffer.resize(600 * 400 * 2);
        lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size(), LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_user_data(display, this);
        lv_display_set_flush_cb(display, [](lv_display_t* disp, const lv_area_t* area, uint8_t* pixels) {
            auto* test = static_cast<SparklineWidget*>(lv_display_get_user_data(disp));
            ++test->flushes;
            const int32_t width = area->x2 - area->x1 + 1;
            const int32_t height = area->y2 - area->y1 + 1;
            for (int32_t row = 0; row < height; row++) {
                memcpy(test->framebuffer.data() +
                           ((area->y1 + row) * 600 + area->x1) * 2,
                       pixels + row * width * 2, width * 2);
            }
            lv_display_flush_ready(disp);
        });
        lv_display_add_event_cb(display, [](lv_event_t* event) {
            ++static_cast<SparklineWidget*>(lv_event_get_user_data(event))->invalidations;
        }, LV_EVENT_INVALIDATE_AREA, this);
        tile = lv_button_create(lv_display_get_screen_active(display));
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
        type->createUI(tile, &config, &button, &rect, &scale, &state);
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


class SparklinePendingStreams : public SparklineWidget {
protected:
    bool initial_streams_available() const override { return false; }
};

TEST_F(SparklineWidget, ResizingRegionReflowsWithoutNewSamples) {
    lv_obj_t* line = chart()->lines[0];
    lv_obj_t* area = lv_obj_get_parent(line);
    auto* points = chart()->points[0];
    const auto stream = chart()->ds_handles[0];
    const int old_width = chart()->cached_chart_w;
    const auto old_last_x = points[3].x;

    lv_obj_set_size(tile, 240, 160);
    lv_obj_update_layout(tile);
    tick();
    lv_obj_update_layout(tile);
    EXPECT_LT(chart()->cached_chart_w, old_width);
    EXPECT_EQ(lv_obj_get_width(area), chart()->cached_chart_w);
    EXPECT_LT(points[3].x, old_last_x);
    EXPECT_EQ(lv_obj_get_width(chart()->lbl_current[0]), chart()->label_margin_r);
    EXPECT_EQ(chart()->lines[0], line);
    EXPECT_EQ(chart()->points[0], points);
    EXPECT_EQ(chart()->ds_handles[0], stream);

    lv_obj_set_size(tile, 4, 4);
    lv_obj_update_layout(tile);
    tick();
    EXPECT_GE(chart()->cached_chart_w, 2);
    EXPECT_GE(chart()->cached_chart_h, 2);

    lv_obj_set_size(tile, 400, 250);
    lv_obj_update_layout(tile);
    tick();
    EXPECT_GT(chart()->cached_chart_w, 2);
    EXPECT_EQ(chart()->lines[0], line);
    EXPECT_EQ(chart()->ds_handles[0], stream);
}

TEST_F(SparklineWidget, SharedContentClipsUpscaledLabelsAndPreservesOffsets) {
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t* label = lv_label_create(tile);
    lv_label_set_text(label, "A long label");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(label, lv_color_hex(0xff0000), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_scale(label, 512, 0);
    ScreenButtonConfig button{};
    button.widget_position = ButtonWidgetPosition::Right;
    button.icon_position = ButtonGroupIconPosition::Above;
    button.group_size_pct = 40;
    button.ui_offset_x = 3;
    button.style_center.x_offset = 2;
    button.style_center.font_upscale = 512;
    strlcpy(button.label_center, "A long label", sizeof(button.label_center));
    strlcpy(button.widget.type, "sparkline", sizeof(button.widget.type));
    lv_obj_t* labels[] = {nullptr, label, nullptr};
    UIScaleInfo scale = pad_get_scale_info();
    ButtonContentLayout layout = pad_apply_content_layout(tile, button, nullptr, labels, scale, nullptr);
    lv_obj_t* region = lv_obj_get_parent(label);
    lv_obj_update_layout(tile);
    EXPECT_EQ(lv_obj_get_x(region), layout.labels[1].x + 5);
    EXPECT_EQ(lv_obj_get_width(label) * 2, layout.labels[1].width - layout.labels[1].width % 2);
    EXPECT_EQ(lv_obj_get_style_transform_pivot_x(label, LV_PART_MAIN), 0);
    EXPECT_EQ(lv_obj_get_style_transform_pivot_y(label, LV_PART_MAIN), 0);
    EXPECT_LE(layout.labels[1].x + layout.labels[1].width, layout.widget.x);
    settle();
    lv_area_t region_area;
    lv_obj_get_coords(region, &region_area);
    const int32_t x = region_area.x2 + 1;
    const int32_t y = region_area.y1 + 1;
    ASSERT_LT(x, 600);
    ASSERT_LT(y, 400);
    const size_t pixel = (y * 600 + x) * 2;
    EXPECT_NE(framebuffer[pixel] | (framebuffer[pixel + 1] << 8), 0xf800);
}

TEST_F(SparklineWidget, AutoContentReflowsFromResolvedLabelText) {
    lv_obj_set_size(tile, 200, 100);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t* label = lv_label_create(tile);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_label_set_text(label, "");
    ScreenButtonConfig button{};
    button.widget_position = ButtonWidgetPosition::Right;
    button.icon_position = ButtonGroupIconPosition::Above;
    button.group_size_pct = 0;
    strlcpy(button.widget.type, "sparkline", sizeof(button.widget.type));
    lv_obj_t* labels[] = {nullptr, label, nullptr};
    UIScaleInfo scale = pad_get_scale_info();
    ButtonContentRuntime runtime{};
    ButtonContentLayout layout = pad_apply_content_layout(tile, button, nullptr, labels, scale, nullptr, &runtime);
    runtime.widget_region = button_content_region(tile, layout.widget, 0, 0);
    EXPECT_EQ(layout.labels[1].height, lv_font_get_line_height(&lv_font_montserrat_14));
    const int initial_x = layout.widget.x;

    pad_set_content_label_text(runtime, 1, "A longer resolved value");
    layout = pad_measure_content_layout(tile, runtime);
    pad_reflow_content_layout(runtime, layout);
    lv_obj_update_layout(tile);
    EXPECT_GT(layout.widget.x, initial_x);
    EXPECT_LE(layout.widget.x, 102);
    EXPECT_EQ(lv_obj_get_x(runtime.widget_region), layout.widget.x);

    pad_set_content_label_text(runtime, 1, "A");
    layout = pad_measure_content_layout(tile, runtime);
    pad_reflow_content_layout(runtime, layout);
    lv_obj_update_layout(tile);
    EXPECT_LT(layout.widget.x, 102);
    EXPECT_EQ(lv_obj_get_x(runtime.widget_region), layout.widget.x);
}

TEST_F(SparklineWidget, CenteredRockerGroupUsesExistingWidgetAndZones) {
    lv_obj_set_size(tile, 300, 180);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_t* label = lv_label_create(tile);
    lv_label_set_text(label, "Value");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_t* labels[] = {nullptr, label, nullptr};
    ScreenButtonConfig button{};
    strlcpy(button.widget.type, "numericrocker", sizeof(button.widget.type));
    strlcpy(button.label_center, "Value", sizeof(button.label_center));
    button.ui_offset_x = 3;
    button.style_center.x_offset = 2;
    button.group_size_pct = 90;
    auto* config = reinterpret_cast<NumericRockerConfig*>(button.widget.data);
    config->horizontal = true;
    config->small_step = 1;
    config->large_step = 10;
    config->zone_scale_pct = 100;
    ButtonContentRuntime runtime{};
    ButtonContentLayout layout = pad_apply_content_layout(tile, button, nullptr, labels,
                                                           pad_get_scale_info(), nullptr, &runtime);
    NRZoneLayout zones = nr_compute_zones(300, 1, 10, 100);
    EXPECT_EQ(layout.widget.width, 300);
    EXPECT_EQ(layout.labels[1].x, zones.inner_end);
    EXPECT_EQ(layout.labels[1].width, zones.inner2_start - zones.inner_end);
    lv_obj_update_layout(tile);
    EXPECT_EQ(lv_obj_get_x(runtime.label_regions[1]), layout.labels[1].x + 5);

    lv_obj_set_size(tile, 100, 180);
    layout = pad_measure_content_layout(tile, runtime);
    zones = nr_compute_zones(100, 1, 10, 100);
    EXPECT_EQ(layout.widget.width, 100);
    EXPECT_EQ(layout.labels[1].width, zones.inner2_start - zones.inner_end);
    pad_reflow_content_layout(runtime, layout);

    lv_obj_delete(label);
    label = lv_label_create(tile);
    lv_label_set_text(label, "Value");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    labels[1] = label;
    config->horizontal = false;
    layout = pad_apply_content_layout(tile, button, nullptr, labels, pad_get_scale_info(), nullptr, &runtime);
    zones = nr_compute_zones(180, 1, 10, 100);
    EXPECT_EQ(layout.widget.height, 180);
    EXPECT_GE(layout.labels[1].y, zones.inner_end);
    EXPECT_LE(layout.labels[1].y + layout.labels[1].height, zones.inner2_start);
}

TEST_F(SparklineWidget, RockerCentersOnBothAxesAndCanUseSplitLayout) {
    lv_obj_set_size(tile, 300, 180);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_t* label = lv_label_create(tile);
    lv_label_set_text(label, "Value");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_t* labels[] = {nullptr, label, nullptr};
    ScreenButtonConfig button{};
    strlcpy(button.widget.type, "rocker", sizeof(button.widget.type));
    strlcpy(button.label_center, "Value", sizeof(button.label_center));
    button.widget.data[0] = 1;
    ButtonContentRuntime runtime{};
    ButtonContentLayout layout = pad_apply_content_layout(tile, button, nullptr, labels,
                                                           pad_get_scale_info(), nullptr, &runtime);
    EXPECT_EQ(layout.widget.width, 300);
    EXPECT_GE(layout.labels[1].x, runtime.indicator_inset);
    EXPECT_LE(layout.labels[1].x + layout.labels[1].width, 300 - runtime.indicator_inset);
    lv_obj_delete(label);

    label = lv_label_create(tile);
    lv_label_set_text(label, "Value");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    labels[1] = label;
    button.widget.data[0] = 0;
    layout = pad_apply_content_layout(tile, button, nullptr, labels, pad_get_scale_info(), nullptr, &runtime);
    EXPECT_EQ(layout.widget.height, 180);
    EXPECT_GE(layout.labels[1].y, runtime.indicator_inset);
    EXPECT_LE(layout.labels[1].y + layout.labels[1].height, 180 - runtime.indicator_inset);
    lv_obj_delete(label);

    label = lv_label_create(tile);
    lv_label_set_text(label, "Value");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    labels[1] = label;
    button.rocker_group_beside = true;
    button.widget_position = ButtonWidgetPosition::Right;
    layout = pad_apply_content_layout(tile, button, nullptr, labels, pad_get_scale_info(), nullptr, &runtime);
    EXPECT_GT(layout.widget.x, 0);
    EXPECT_LE(layout.labels[1].x + layout.labels[1].width, layout.widget.x);
}

TEST_F(SparklineWidget, AutoDotLabelsRecoverAfterTextAndBoundsGrow) {
    lv_obj_set_size(tile, 200, 100);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_t* label = lv_label_create(tile);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, "A");
    ScreenButtonConfig button{};
    strlcpy(button.label_center, "A", sizeof(button.label_center));
    strlcpy(button.widget.type, "sparkline", sizeof(button.widget.type));
    button.widget_position = ButtonWidgetPosition::Right;
    button.style_center.long_mode = LABEL_MODE_DOT;
    lv_obj_t* labels[] = {nullptr, label, nullptr};
    ButtonContentRuntime runtime{};
    pad_apply_content_layout(tile, button, nullptr, labels, pad_get_scale_info(), nullptr, &runtime);
    const char* text = "Very long text that should retain its measured width";
    pad_set_content_label_text(runtime, 1, text);
    pad_reflow_content_layout(runtime, pad_measure_content_layout(tile, runtime));
    lv_obj_update_layout(tile);
    EXPECT_LT(lv_obj_get_width(label), runtime.input.label_width[1]);
    EXPECT_STRNE(lv_label_get_text(label), text);
    const int natural_width = runtime.input.label_width[1];

    lv_obj_set_size(tile, 800, 100);
    pad_reflow_content_layout(runtime, pad_measure_content_layout(tile, runtime));
    lv_obj_update_layout(tile);
    EXPECT_EQ(lv_obj_get_width(label), natural_width);
    EXPECT_STREQ(lv_label_get_text(label), text);
}

TEST_F(SparklineWidget, ReflowRestoresIconScaleAfterRegionGrows) {
    static const uint8_t pixels[24 * 24 * 2] = {};
    lv_image_dsc_t image{};
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_RGB565;
    image.header.w = image.header.h = 24;
    image.header.stride = 48;
    image.data_size = sizeof(pixels);
    image.data = pixels;
    lv_obj_set_size(tile, 200, 100);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_t* icon = lv_image_create(tile);
    lv_image_set_src(icon, &image);
    lv_image_set_scale(icon, 512);
    ScreenButtonConfig button{};
    button.widget_position = ButtonWidgetPosition::Right;
    button.group_size_pct = 10;
    strlcpy(button.widget.type, "sparkline", sizeof(button.widget.type));
    lv_obj_t* labels[] = {nullptr, nullptr, nullptr};
    UIScaleInfo scale = pad_get_scale_info();
    ButtonContentRuntime runtime{};
    pad_apply_content_layout(tile, button, icon, labels, scale, nullptr, &runtime);
    const uint32_t small_scale = lv_image_get_scale_x(icon);
    EXPECT_LT(small_scale, 512u);

    runtime.input.group_size_pct = 60;
    pad_reflow_content_layout(runtime, pad_measure_content_layout(tile, runtime));
    EXPECT_EQ(lv_image_get_scale_x(icon), 512u);
    runtime.input.group_size_pct = 10;
    pad_reflow_content_layout(runtime, pad_measure_content_layout(tile, runtime));
    EXPECT_EQ(lv_image_get_scale_x(icon), small_scale);
}

TEST_F(SparklineWidget, GaugeResizeReusesTicksAndKeepsCaptionInsideRegion) {
    lv_obj_t* region = button_content_region(tile, {0, 0, 240, 180}, 0, 0);
    WidgetConfig config{};
    WidgetState state{};
    ScreenButtonConfig button{};
    PadRect rect{0, 0, 240, 180};
    UIScaleInfo scale = pad_get_scale_info();
    JsonDocument document;
    document["widget_gauge_start_label"] = "Power";
    document["widget_gauge_ticks"] = 4;
    gauge_parse(document.as<JsonObject>(), config.data);
    gauge_create(region, &config, &button, &rect, &scale, &state);
    auto* gauge = reinterpret_cast<GaugeState*>(state.data);
    ASSERT_GT(gauge->tick_line_count, 0);
    ASSERT_NE(gauge->start_label_1, nullptr);
    gauge_tick(region, &config, &state);
    const unsigned initial_calls = gauge_trig_calls;
    for (int i = 0; i < 10; ++i) gauge_tick(region, &config, &state);
    EXPECT_EQ(gauge_trig_calls, initial_calls);

    unsigned deleted = 0;
    auto on_delete = [](lv_event_t* event) { ++*static_cast<unsigned*>(lv_event_get_user_data(event)); };
    std::vector<lv_obj_t*> ticks(gauge->tick_lines, gauge->tick_lines + gauge->tick_line_count);
    for (lv_obj_t* line : ticks) lv_obj_add_event_cb(line, on_delete, LV_EVENT_DELETE, &deleted);
    lv_obj_set_size(region, 160, 100);
    gauge_tick(region, &config, &state);
    EXPECT_EQ(deleted, 0u);
    EXPECT_GT(gauge_trig_calls, initial_calls);
    EXPECT_EQ(gauge->tick_line_count, ticks.size());
    for (unsigned i = 0; i < ticks.size(); ++i) {
        EXPECT_EQ(gauge->tick_lines[i], ticks[i]);
        lv_obj_remove_event_cb_with_user_data(ticks[i], on_delete, &deleted);
    }
    lv_obj_update_layout(region);
    const lv_area_t bounds = widget_content_bounds(gauge->start_label_1);
    const int x = lv_obj_get_x(gauge->start_label_1), y = lv_obj_get_y(gauge->start_label_1);
    EXPECT_GE(x + bounds.x1, 0);
    EXPECT_GE(y + bounds.y1, 0);
    EXPECT_LT(x + bounds.x2, lv_obj_get_content_width(region));
    EXPECT_LT(y + bounds.y2, lv_obj_get_content_height(region));
    gauge_destroy(&state);
}

TEST_F(SparklineWidget, BarChartResizeMatchesCreationGeometryWithDualCaptions) {
    WidgetConfig config{};
    ScreenButtonConfig button{};
    PadRect rect{0, 0, 200, 140};
    UIScaleInfo scale = pad_get_scale_info();
    JsonDocument document;
    document["widget_bar_label"] = "Near";
    document["widget_bar_label_2"] = "Far";
    document["widget_bar_dual_binding_pair_1"] = true;
    strlcpy(config.data_binding[1], "second", sizeof(config.data_binding[1]));
    for (bool horizontal : {false, true}) {
        document["widget_orientation"] = horizontal ? "horizontal" : "vertical";
        bar_chart_parse(document.as<JsonObject>(), config.data);
        lv_obj_t* resized = button_content_region(tile, {0, 0, 300, 200}, 0, 0);
        lv_obj_t* fresh = button_content_region(tile, {0, 0, rect.w, rect.h}, 0, 0);
        struct StyleRecord {
            ButtonFillStyle style;
            char color[8];
        } style{};
        style.style.bytes = sizeof(style);
        style.style.track_radius = 64;
        strcpy(style.color, "#000000");
        for (auto& offset : style.style.color_offsets) offset = offsetof(StyleRecord, color);
        ASSERT_NE(fill_style_create(resized, &style.style), nullptr);
        ASSERT_NE(fill_style_create(fresh, &style.style), nullptr);
        WidgetState resized_state{}, fresh_state{};
        bar_chart_create(resized, &config, &button, &rect, &scale, &resized_state);
        bar_chart_create(fresh, &config, &button, &rect, &scale, &fresh_state);
        lv_obj_set_size(resized, rect.w, rect.h);
        bar_chart_tick(resized, &config, &resized_state);
        lv_obj_update_layout(tile);
        auto* actual = reinterpret_cast<BarChartState*>(resized_state.data);
        auto* expected = reinterpret_cast<BarChartState*>(fresh_state.data);
        ASSERT_EQ(actual->num_bars, 2);
        for (int i = 0; i < 2; ++i) {
            EXPECT_EQ(actual->slots[i].track_total, expected->slots[i].track_total);
            EXPECT_EQ(actual->slots[i].track_cross, expected->slots[i].track_cross);
            EXPECT_EQ(lv_obj_get_style_radius(actual->slots[i].fill, LV_PART_MAIN),
                      lv_obj_get_style_radius(expected->slots[i].fill, LV_PART_MAIN));
            ASSERT_NE(actual->slots[i].label, nullptr);
            EXPECT_EQ(lv_obj_get_x(actual->slots[i].label), lv_obj_get_x(expected->slots[i].label));
            EXPECT_EQ(lv_obj_get_y(actual->slots[i].label), lv_obj_get_y(expected->slots[i].label));
        }
        bar_chart_destroy(&resized_state);
        bar_chart_destroy(&fresh_state);
        lv_obj_delete(resized);
        lv_obj_delete(fresh);
    }
}

TEST_F(SparklineWidget, TableResizePreservesConfiguredAndAutomaticColumnWidths) {
    lv_obj_t* region = button_content_region(tile, {0, 0, 200, 140}, 0, 0);
    WidgetConfig config{};
    WidgetState state{};
    ScreenButtonConfig button{};
    PadRect rect{0, 0, 200, 140};
    UIScaleInfo scale = pad_get_scale_info();
    table_create(region, &config, &button, &rect, &scale, &state);
    const char* payload = R"({"columns":[{"key":"name","width_pct":25},{"key":"value"}],"rows":[{"name":"A","value":10}]})";
    table_update(region, &config, &state, payload);
    auto* table = table_get_state(&state);
    ASSERT_EQ(table->col_count, 2);
    const int original_width = lv_obj_get_width(table->table);
    EXPECT_EQ(lv_table_get_column_width(table->table, 0), original_width / 4);
    EXPECT_EQ(lv_table_get_column_width(table->table, 1), original_width - original_width / 4);
    lv_obj_set_size(region, 280, 140);
    lv_obj_update_layout(region);
    const int width = lv_obj_get_width(table->table);
    EXPECT_GT(width, original_width);
    EXPECT_EQ(lv_table_get_column_width(table->table, 0), width / 4);
    EXPECT_EQ(lv_table_get_column_width(table->table, 1), width - width / 4);
    table_destroy(&state);
}

TEST_F(SparklineWidget, ChartAndAnnotationsUseAssignedRegion) {
    lv_obj_t* region = button_content_region(tile, {40, 30, 120, 90}, 0, 0);
    WidgetState region_state{};
    ScreenButtonConfig button{};
    PadRect rect{};
    rect.w = 580;
    rect.h = 380;
    UIScaleInfo scale{};
    scale.font_small = LV_FONT_DEFAULT;
    type->createUI(region, &config, &button, &rect, &scale, &region_state);
    auto* region_chart = reinterpret_cast<SparklineState*>(region_state.data);
    ASSERT_NE(region_chart->lines[0], nullptr);
    type->update(region, &config, &region_state, "ignored");
    lv_obj_update_layout(region);
    EXPECT_EQ(lv_obj_get_parent(lv_obj_get_parent(region_chart->lines[0])), region);
    EXPECT_LE(region_chart->cached_chart_w + region_chart->label_margin_r, 120);
    ASSERT_NE(region_chart->lbl_current[0], nullptr);
    EXPECT_EQ(lv_obj_get_parent(region_chart->lbl_current[0]), region);
    EXPECT_GE(lv_obj_get_x(region_chart->lbl_current[0]), 0);
    EXPECT_LE(lv_obj_get_x(region_chart->lbl_current[0]) +
              lv_obj_get_width(region_chart->lbl_current[0]), 120);
    type->destroyUI(&region_state);
}

TEST_F(SparklineWidget, InitiallySmallRegionCanGrow) {
    lv_obj_t* region = button_content_region(tile, {0, 0, 1, 1}, 0, 0);
    WidgetState region_state{};
    ScreenButtonConfig button{};
    PadRect rect{};
    UIScaleInfo scale{};
    scale.font_small = LV_FONT_DEFAULT;
    type->createUI(region, &config, &button, &rect, &scale, &region_state);
    auto* region_chart = reinterpret_cast<SparklineState*>(region_state.data);
    ASSERT_NE(region_chart->lines[0], nullptr);
    lv_obj_set_size(region, 120, 90);
    lv_obj_update_layout(region);
    type->tick(region, &config, &region_state);
    EXPECT_GT(region_chart->cached_chart_w, 2);
    EXPECT_GT(region_chart->cached_chart_h, 2);
    EXPECT_NE(region_chart->draw_context[0].count, 0);
    type->destroyUI(&region_state);
}

TEST_F(SparklinePendingStreams, TickConnectsLateStreamsWithoutAnotherSaveOrRevisionChange) {
    for (uint8_t i = 0; i < 3; i++) {
        ASSERT_EQ(chart()->ds_handles[i], DATA_STREAM_INVALID);
        chart()->cached_revs[i] = streams[i].rev;
    }
    chart()->label_margin_r = 0;
    tick();
    EXPECT_EQ(flushes, 0u);
    streams_available = true;
    tick();
    for (uint8_t i = 0; i < 3; i++) {
        EXPECT_EQ(chart()->ds_handles[i], i);
        EXPECT_GT(lv_line_get_point_count(chart()->lines[i]), 0u);
    }
    settle();
    tick();
    lv_refr_now(display);
    EXPECT_EQ(flushes, 0u);
}

TEST_F(SparklinePendingStreams, UpdateConnectsLateStreamsWithoutRecreatingWidget) {
    streams_available = true;
    update();
    for (uint8_t i = 0; i < 3; i++) {
        EXPECT_EQ(chart()->ds_handles[i], i);
        EXPECT_GT(lv_line_get_point_count(chart()->lines[i]), 0u);
    }
}

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

TEST_F(SparklineWidget, SharedRawHistoryHoursReachEveryStream) {
    auto hours_for = [&](uint8_t line) {
        uint16_t hours = 999;
        EXPECT_TRUE(type->getStreamParams(&config, line, nullptr, nullptr,
                                          nullptr, nullptr, &hours));
        return hours;
    };
    EXPECT_EQ(hours_for(0), 0);
    EXPECT_EQ(hours_for(1), 0);
    EXPECT_EQ(hours_for(2), 0);
    JsonDocument document;
    auto fields = document.to<JsonObject>();
    fields["widget_sparkline_raw_hours"] = 24;
    fields["widget_sparkline_raw_hours_2"] = 72;
    fields["widget_sparkline_raw_hours_3"] = 720;
    type->parseConfig(fields, config.data);
    EXPECT_EQ(hours_for(0), 24);
    EXPECT_EQ(hours_for(1), 24);
    EXPECT_EQ(hours_for(2), 24);
    for (double invalid : {-1.0, 1.5, 721.0}) {
        fields["widget_sparkline_raw_hours"] = invalid;
        type->parseConfig(fields, config.data);
        EXPECT_EQ(hours_for(0), 0);
        EXPECT_EQ(hours_for(1), 0);
        EXPECT_EQ(hours_for(2), 0);
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
    EXPECT_EQ(lv_line_get_point_count(chart()->lines[0]), 4u);
    EXPECT_EQ(chart()->points[0][2].y, -1);
    EXPECT_GT(invalidations, 0u);
}

TEST_F(SparklineWidget, GapsBreakDrawingSmoothingAndCurrentValue) {
    for (unsigned line = 0; line < 3; line++) {
        for (float& sample : samples[line]) sample = NAN;
        streams[line].auto_min = 0;
        streams[line].auto_max = 100;
        streams[line].last_value = NAN;
        ++streams[line].rev;
    }
    samples[0][0] = 10;
    samples[0][1] = 30;
    samples[0][3] = 90;
    streams[0].last_value = NAN;

    update();
    ASSERT_EQ(lv_line_get_point_count(chart()->lines[0]), 4u);
    EXPECT_EQ(chart()->points[0][2].y, -1);
    EXPECT_GT(chart()->points[0][0].y, chart()->points[0][1].y);

    settle();
    lv_area_t line_bounds;
    lv_obj_get_coords(chart()->lines[0], &line_bounds);
    const auto& points = chart()->points[0];
    const uint16_t expected_color = lv_color_to_u16(lv_color_hex(0x123456));
    auto has_color_near = [&](int x, int y) {
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                const int px = x + dx, py = y + dy;
                if (px < 0 || px >= 600 || py < 0 || py >= 400) continue;
                uint16_t pixel;
                memcpy(&pixel, framebuffer.data() + (py * 600 + px) * 2, sizeof(pixel));
                if (pixel == expected_color) return true;
            }
        }
        return false;
    };
    const int connected_x = line_bounds.x1 +
                            (points[0].x + points[1].x) / 2;
    const int connected_y = line_bounds.y1 +
                            (points[0].y + points[1].y) / 2;
    EXPECT_TRUE(has_color_near(connected_x, connected_y));

    const int gap_x = line_bounds.x1 + (points[1].x + points[3].x) / 2;
    const int gap_y = line_bounds.y1 + (points[1].y + points[3].y) / 2;
    EXPECT_FALSE(has_color_near(gap_x, gap_y));

    samples[0][3] = NAN;
    ++streams[0].rev;
    update();
    EXPECT_EQ(chart()->points[0][3].y, -1);
    EXPECT_TRUE(lv_obj_has_flag(chart()->dot_current[0], LV_OBJ_FLAG_HIDDEN));
    EXPECT_TRUE(lv_obj_has_flag(chart()->lbl_current[0], LV_OBJ_FLAG_HIDDEN));
}

TEST_F(SparklineWidget, AllGapHistoryRendersNoCurrentDotAcrossAllLines) {
    for (unsigned line = 0; line < 3; line++) {
        for (float& sample : samples[line]) sample = NAN;
        ++streams[line].rev;
    }
    update();
    for (unsigned line = 0; line < 3; line++) {
        EXPECT_EQ(lv_line_get_point_count(chart()->lines[line]), 4u);
        for (unsigned slot = 0; slot < 4; slot++)
            EXPECT_EQ(chart()->points[line][slot].y, -1);
        EXPECT_TRUE(lv_obj_has_flag(chart()->dot_current[line], LV_OBJ_FLAG_HIDDEN));
    }
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
