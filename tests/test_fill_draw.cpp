#include <gtest/gtest.h>
#include <lvgl.h>
#include <array>
#include "Arduino.h"
#include "board_config.h"
#undef HAS_MQTT
#define HAS_MQTT 0
#include "fill_draw.h"
#include "fill_style_runtime.h"

class FillDraw : public testing::Test {
protected:
    lv_display_t* display = nullptr;
    lv_obj_t* object = nullptr;
    std::array<uint8_t, 64 * 64 * 3> buffer{};
    std::array<uint8_t, 64 * 64 * 3> pixels{};
    FillPattern pattern = FillPattern::Diagonal;
    bool outline = false;

    void SetUp() override {
        static bool initialized = false;
        if (!initialized) { lv_init(); initialized = true; }
        display = lv_display_create(64, 64);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
        lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size(), LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_user_data(display, this);
        lv_display_set_flush_cb(display, [](lv_display_t* output, const lv_area_t* area, uint8_t* data) {
            auto* fixture = static_cast<FillDraw*>(lv_display_get_user_data(output));
            int stride = lv_draw_buf_width_to_stride(lv_area_get_width(area), LV_COLOR_FORMAT_RGB888);
            for (int vertical = area->y1; vertical <= area->y2; ++vertical) {
                memcpy(fixture->pixels.data() + (vertical * 64 + area->x1) * 3,
                       data + (vertical - area->y1) * stride, lv_area_get_width(area) * 3);
            }
            lv_display_flush_ready(output);
        });
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_white(), 0);
        object = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(object);
        lv_obj_set_pos(object, 8, 8);
        lv_obj_set_size(object, 48, 48);
        lv_obj_set_style_bg_color(object, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(object, [](lv_event_t* event) {
            auto* fixture = static_cast<FillDraw*>(lv_event_get_user_data(event));
            lv_layer_t* layer = lv_event_get_layer(event);
            if (fixture->outline) {
                lv_point_t center = {32, 32};
                fill_draw_arc_outline(layer, center, 20, 8, 0, 180, 2, lv_color_black());
            } else {
                lv_area_t bounds;
                lv_obj_get_coords(fixture->object, &bounds);
                lv_point_t origin = {8, 8};
                fill_draw_pattern(layer, bounds, 8, origin, fixture->pattern, lv_color_black(), 8, 1);
            }
        }, LV_EVENT_DRAW_MAIN_END, this);
    }
    void TearDown() override { lv_display_delete(display); }
    bool black(int horizontal, int vertical) const {
        return pixels[(vertical * 64 + horizontal) * 3] == 0;
    }
    void render() { lv_obj_invalidate(object); lv_refr_now(display); }
};

TEST_F(FillDraw, PatternClipsCornersAndSurvivesPartialRedraw) {
    render();
    EXPECT_FALSE(black(8, 8));
    EXPECT_FALSE(black(7, 24));
    EXPECT_TRUE(black(24, 24));
    EXPECT_FALSE(black(25, 24));
    auto previous = pixels;
    lv_area_t partial = {19, 17, 38, 34};
    lv_obj_invalidate_area(object, &partial);
    lv_refr_now(display);
    EXPECT_EQ(previous, pixels);
}

TEST_F(FillDraw, NoneDoesNotAddTexture) {
    pattern = FillPattern::None;
    render();
    EXPECT_FALSE(black(24, 24));
}

TEST_F(FillDraw, RuntimeCopiesColorsAndReleasesRebuiltButtons) {
    struct StyleRecord {
        ButtonFillStyle style;
        char color[8];
    } record{};
    record.style.bytes = sizeof(record);
    record.style.patterns[0] = FillPattern::Crosshatch;
    record.style.patterns[5] = FillPattern::Dots;
    record.style.spacing = 2;
    record.style.stroke = 1;
    record.style.outline_width = 1;
    for (auto& offset : record.style.color_offsets) offset = offsetof(StyleRecord, color);
    render();
    lv_mem_monitor_t before;
    lv_mem_monitor(&before);
    for (unsigned iteration = 0; iteration < 30; ++iteration) {
        strcpy(record.color, "#123456");
        lv_obj_t* button = lv_obj_create(lv_screen_active());
        lv_obj_set_size(button, 48, 48);
        auto* runtime = fill_style_create(button, &record.style);
        ASSERT_NE(runtime, nullptr);
        EXPECT_EQ(fill_style_create(button, &record.style), runtime);
        EXPECT_EQ(fill_style_find(button), runtime);
        strcpy(record.color, "#FFFFFF");
        fill_style_poll(runtime);
        EXPECT_STREQ(runtime->style()->color(0), "#123456");
        EXPECT_EQ(runtime->colors[0], 0x123456u);
        lv_obj_t* track = lv_obj_create(button);
        lv_obj_set_size(track, 32, 8);
        fill_style_attach_pattern(track, runtime, 5);
        fill_style_attach_rect_outline(track, runtime);
        lv_refr_now(display);
        lv_obj_delete(button);
        lv_refr_now(display);
    }
    lv_mem_monitor_t after;
    lv_mem_monitor(&after);
    EXPECT_EQ(after.used_cnt, before.used_cnt);
    EXPECT_EQ(after.free_size, before.free_size);
}

TEST_F(FillDraw, RuntimeDetectsSchemesInEveryStyleColor) {
    struct StyleRecord {
        ButtonFillStyle style;
        char colors[7][96];
    } record{};
    record.style.bytes = sizeof(record);
    for (unsigned index = 0; index < 7; ++index) {
        record.style.color_offsets[index] = offsetof(StyleRecord, colors) + index * sizeof(record.colors[0]);
        strcpy(record.colors[index], "#000000");
    }
    EXPECT_FALSE(fill_style_has_scheme(nullptr, "[time:"));
    for (unsigned index = 0; index < 7; ++index) {
        strcpy(record.colors[index], "[expr:threshold([shutter:count], \"#000000\", [time:%M], \"#FFFFFF\")]");
        lv_obj_t* button = lv_obj_create(object);
        auto* runtime = fill_style_create(button, &record.style);
        ASSERT_NE(runtime, nullptr);
        EXPECT_TRUE(fill_style_has_scheme(runtime, "[shutter:"));
        EXPECT_TRUE(fill_style_has_scheme(runtime, "[time:"));
        EXPECT_FALSE(fill_style_has_scheme(runtime, "[mqtt:"));
        EXPECT_FALSE(fill_style_has_scheme(runtime, nullptr));
        EXPECT_FALSE(fill_style_has_scheme(runtime, ""));
        lv_obj_delete(button);
        strcpy(record.colors[index], "#000000");
    }
}

TEST_F(FillDraw, GaugeOutlineFollowsBothEdgesAndEnds) {
    outline = true;
    render();
    EXPECT_TRUE(black(32, 51));
    EXPECT_TRUE(black(32, 44));
    EXPECT_FALSE(black(32, 48));
    EXPECT_TRUE(black(48, 32));
    EXPECT_FALSE(black(32, 12));
    EXPECT_FALSE(black(12, 12));
}

TEST_F(FillDraw, TrackOutlineRemainsAboveEmptyAndFullFill) {
    pattern = FillPattern::None;
    struct StyleRecord {
        ButtonFillStyle style;
        char color[8];
    } record{};
    record.style.bytes = sizeof(record);
    record.style.spacing = 8;
    record.style.stroke = 1;
    record.style.outline_width = 2;
    record.style.patterns[1] = FillPattern::Diagonal;
    strcpy(record.color, "#000000");
    for (auto& offset : record.style.color_offsets) offset = offsetof(StyleRecord, color);
    auto* runtime = fill_style_create(object, &record.style);
    ASSERT_NE(runtime, nullptr);
    lv_obj_t* track = lv_obj_create(object);
    lv_obj_remove_style_all(track);
    lv_obj_set_pos(track, 8, 16);
    lv_obj_set_size(track, 32, 12);
    lv_obj_set_style_radius(track, 4, 0);
    lv_obj_set_style_clip_corner(track, true, 0);
    lv_obj_t* fill = lv_obj_create(track);
    lv_obj_remove_style_all(fill);
    lv_obj_set_size(fill, 0, 12);
    lv_obj_set_style_bg_color(fill, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    fill_style_attach_pattern(fill, runtime, 1);
    fill_style_attach_rect_outline(track, runtime);
    render();
    EXPECT_TRUE(black(16, 30));
    EXPECT_TRUE(black(47, 30));
    EXPECT_FALSE(black(24, 32));
    lv_obj_set_width(fill, 32);
    render();
    EXPECT_TRUE(black(16, 30));
    EXPECT_TRUE(black(47, 30));
    EXPECT_TRUE(black(24, 32));
}

TEST_F(FillDraw, RoundedTracksClipDualFillsWithoutRoundingTheirBaseline) {
    pattern = FillPattern::None;
    for (int requested_radius : {0, 4, 64}) {
        int radius = fill_geometry_clamp(requested_radius, 0, 6);
        lv_obj_t* track = lv_obj_create(object);
        lv_obj_remove_style_all(track);
        lv_obj_set_pos(track, 8, 16);
        lv_obj_set_size(track, 32, 12);
        lv_obj_set_style_radius(track, radius, 0);
        lv_obj_set_style_clip_corner(track, true, 0);
        for (int partner = 0; partner < 2; ++partner) {
            lv_obj_t* fill = lv_obj_create(track);
            lv_obj_remove_style_all(fill);
            lv_obj_set_pos(fill, partner * 16, 0);
            lv_obj_set_size(fill, 16, 12);
            lv_obj_set_style_bg_color(fill, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(fill, radius, 0);
            lv_obj_t* cap = lv_obj_create(fill);
            lv_obj_remove_style_all(cap);
            lv_obj_set_size(cap, radius, 12);
            lv_obj_align(cap, partner ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_set_style_bg_color(cap, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
        }
        render();
        EXPECT_EQ(black(16, 24), radius == 0);
        EXPECT_EQ(black(47, 24), radius == 0);
        EXPECT_TRUE(black(31, 24));
        EXPECT_TRUE(black(32, 24));
        EXPECT_TRUE(black(32, 35));
        EXPECT_FALSE(black(15, 30));
        lv_obj_delete(track);
    }
}