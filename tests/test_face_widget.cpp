#include <gtest/gtest.h>
#include <array>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "expr_binding.h"
#include "widgets/face_widget.cpp"

void widget_register(const WidgetType*) {}
static const char* face_report = nullptr;
static constexpr bool static_face = DISPLAY_DISABLE_ANIMATIONS && !HAS_LVGL_EPAPER;

class FaceWidget : public testing::Test {
protected:
    static constexpr int width = 192, height = 160;
    using Pixels = std::array<uint8_t, width * height * 3>;
    Pixels buffer{}, pixels{};
    lv_display_t* display = nullptr;
    lv_obj_t* button = nullptr;
    WidgetConfig config{};
    WidgetState runtime{};
    ScreenButtonConfig source{};
    JsonDocument json;
    bool created = false;
    uint64_t flushed_pixels = 0;

    void SetUp() override {
        face_report = nullptr;
        static bool initialized = false;
        if (!initialized) { lv_init(); initialized = true; }
        display = lv_display_create(width, height);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
        lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size(), LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_user_data(display, this);
        lv_display_set_flush_cb(display, [](lv_display_t* output, const lv_area_t* area, uint8_t* data) {
            auto* fixture = static_cast<FaceWidget*>(lv_display_get_user_data(output));
            const int stride = lv_draw_buf_width_to_stride(lv_area_get_width(area), LV_COLOR_FORMAT_RGB888);
            fixture->flushed_pixels += lv_area_get_size(area);
            for (int row = area->y1; row <= area->y2; ++row)
                memcpy(fixture->pixels.data() + (row * width + area->x1) * 3,
                       data + (row - area->y1) * stride, lv_area_get_width(area) * 3);
            lv_display_flush_ready(output);
        });
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x102030), 0);
        button = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(button);
        lv_obj_set_pos(button, 8, 8);
        lv_obj_set_size(button, width - 16, height - 16);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x102030), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(button, lv_color_hex(0x80e8ff), 0);
    }

    void TearDown() override {
        if (created) face_widget_type.destroyUI(&runtime);
        lv_display_delete(display);
    }

    void create(const char* style = "robot", const char* emotion = "content") {
        if (created) face_widget_type.destroyUI(&runtime);
        json["widget_face_style"] = style;
        json["widget_face_emotion"] = emotion;
        parse();
        face_widget_type.createUI(button, &config, &source, nullptr, nullptr, nullptr, nullptr, &runtime);
        created = true;
    }

    void parse() {
        face_widget_type.parseConfig(json.as<JsonObject>(), config.data);
        strlcpy(config.data_binding[0], json["widget_data_binding"] | "", sizeof(config.data_binding[0]));
    }
    void emotion(const char* value) { face_widget_type.update(button, &config, &runtime, value); tick(0); }
    void tick(uint32_t elapsed) {
        lv_tick_inc(elapsed);
        face_widget_type.tick(button, &config, &runtime);
    }
    Pixels render() { lv_refr_now(display); return pixels; }

    std::pair<size_t, size_t> visible_pupils() {
        // Pupils are background-colored holes, disconnected from the outer background.
        const auto background = [&](size_t index) {
            return pixels[index * 3] == 0x30 && pixels[index * 3 + 1] == 0x20 &&
                pixels[index * 3 + 2] == 0x10;
        };
        std::array<bool, width * height> outside{};
        std::vector<size_t> pending{0};
        outside[0] = true;
        for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
            const int x = pending[cursor] % width, y = pending[cursor] / width;
            for (const auto& step : {std::pair<int, int>{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
                const int next_x = x + step.first, next_y = y + step.second;
                if (next_x < 0 || next_x >= width || next_y < 0 || next_y >= height) continue;
                const size_t index = next_y * width + next_x;
                if (outside[index] || !background(index)) continue;
                outside[index] = true;
                pending.push_back(index);
            }
        }
        std::pair<size_t, size_t> count{};
        for (int y = height / 4; y < height * 2 / 3; ++y) {
            for (int x = 0; x < width; ++x) {
                const size_t index = y * width + x;
                if (!outside[index] && background(index))
                    ++(x < width / 2 ? count.first : count.second);
            }
        }
        return count;
    }

    void save(const std::string& name) {
        const char* directory = std::getenv("FACE_WIDGET_PREVIEW_DIR");
        if (!directory || static_face) return;
        std::ofstream file(std::string(directory) + "/" + name + ".ppm", std::ios::binary);
        ASSERT_TRUE(file.is_open());
        file << "P6\n" << width << " " << height << "\n255\n";
        for (size_t offset = 0; offset < pixels.size(); offset += 3) {
            const char rgb[] = {static_cast<char>(pixels[offset + 2]), static_cast<char>(pixels[offset + 1]),
                                static_cast<char>(pixels[offset])};
            file.write(rgb, sizeof(rgb));
        }
        ASSERT_TRUE(file.good());
    }
};

TEST_F(FaceWidget, EveryStyleAndEmotionProducesADistinctBoundedExpression) {
    for (const char* style : {"robot", "doodle"}) {
        std::set<Pixels> expressions;
        for (const char* value : face_emotions) {
            create(style, value);
            tick(1000);
            render();
            expressions.insert(pixels);
            save(std::string(style) + "_" + value);
            size_t colored = 0;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t offset = (y * width + x) * 3;
                    if (pixels[offset] == 0x30 && pixels[offset + 1] == 0x20 && pixels[offset + 2] == 0x10) continue;
                    ++colored;
                    EXPECT_GT(x, 8);
                    EXPECT_LT(x, width - 9);
                    EXPECT_GT(y, 8);
                    EXPECT_LT(y, height - 9);
                }
            }
            EXPECT_GT(colored, 20u) << style << " " << value;
        }
        EXPECT_EQ(expressions.size(), 8u) << style;
    }
}

TEST_F(FaceWidget, EmotionTransitionsRetargetContinuouslyAndRepeatedTargetsSettle) {
    create();
    const auto content = render();
    emotion("angry");
    const auto start = render();
    if (static_face) {
        EXPECT_NE(content, start);
        tick(1000);
        EXPECT_EQ(start, render());
        return;
    }
    EXPECT_EQ(content, start);
    tick(198);
    const auto halfway = render();
    EXPECT_NE(content, halfway);
    emotion("happy");
    EXPECT_EQ(halfway, render());
    for (unsigned index = 0; index < 15; ++index) {
        emotion("happy");
        tick(33);
    }
    const auto settled = render();
    tick(100);
    EXPECT_EQ(settled, render());
    EXPECT_NE(settled, halfway);
}

TEST_F(FaceWidget, HappyAndSleepyKeepVisiblePupilsUntilTheyBlink) {
    for (const char* style : {"robot", "doodle"}) {
        for (const char* value : {"happy", "sleepy"}) {
            SCOPED_TRACE(std::string(style) + " " + value);
            create(style, value);
            tick(1000);
            auto& state = *reinterpret_cast<FaceState*>(runtime.data);
            state.eye_open = 1;
            for (float gaze : {-1.0f, 0.0f, 1.0f}) {
                SCOPED_TRACE(gaze);
                state.gaze_x = gaze * 7.5f;
                state.gaze_y = gaze * 4.5f;
                lv_obj_invalidate(state.object);
                render();
                const auto pupils = visible_pupils();
                EXPECT_GT(pupils.first, 20u);
                EXPECT_GT(pupils.second, 20u);
            }
            state.eye_open = 0.04f;
            lv_obj_invalidate(state.object);
            render();
            EXPECT_EQ(visible_pupils(), (std::pair<size_t, size_t>{0, 0}));
        }
    }
}

TEST_F(FaceWidget, StillSuppressesIdleRedrawsButPreservesEmotionTransitions) {
    json["widget_face_liveliness"] = "still";
    ASSERT_EQ(face_widget_type.validateConfig(json.as<JsonObject>()), nullptr);
    for (const char* style : {"robot", "doodle"}) {
        SCOPED_TRACE(style);
        create(style);
        const auto initial = render();
        flushed_pixels = 0;
        for (unsigned index = 0; index < 12; ++index) {
            tick(1000);
            EXPECT_EQ(initial, render());
        }
        EXPECT_EQ(flushed_pixels, 0u);
        EXPECT_GT(visible_pupils().first, 20u);
        EXPECT_GT(visible_pupils().second, 20u);

        emotion("angry");
        if (!static_face) {
            EXPECT_EQ(initial, render());
        }
        tick(198);
        const auto halfway = render();
        EXPECT_NE(initial, halfway);
        tick(1000);
        const auto settled = render();
        if (!static_face) {
            EXPECT_NE(halfway, settled);
        }

        face_widget_type.onHide(&runtime);
        tick(60000);
        face_widget_type.onShow(&runtime);
        render();
        flushed_pixels = 0;
        for (unsigned index = 0; index < 12; ++index) {
            tick(1000);
            EXPECT_EQ(settled, render());
        }
        EXPECT_EQ(flushed_pixels, 0u);
    }
}

TEST_F(FaceWidget, IdleAnimationPausesOnHideAndSurvivesClockWrap) {
    lv_tick_inc(UINT32_MAX - lv_tick_get() - 1500);
    create();
    const auto initial = render();
    bool changed = false;
    for (unsigned index = 0; index < 120; ++index) {
        tick(66);
        if (render() != initial) changed = true;
    }
    EXPECT_EQ(changed, !static_face);
    face_widget_type.onHide(&runtime);
    const auto hidden = render();
    tick(60000);
    EXPECT_EQ(hidden, render());
    face_widget_type.onShow(&runtime);
    render();
    face_widget_type.destroyUI(&runtime);
    tick(100);
    created = false;
    render();
}

TEST_F(FaceWidget, InheritsLiveButtonColorsAndDoesNotRedrawAtRest) {
    create();
    render();
    for (int y = 75; y <= 85; ++y) {
        for (int x = 39; x <= 43; ++x) {
            const size_t offset = (y * width + x) * 3;
            EXPECT_EQ(pixels[offset], 0xff);
            EXPECT_EQ(pixels[offset + 1], 0xe8);
            EXPECT_EQ(pixels[offset + 2], 0x80);
        }
    }
    unsigned invalidations = 0;
    const auto observe = [](lv_event_t* event) {
        ++*static_cast<unsigned*>(lv_event_get_user_data(event));
    };
    lv_display_add_event_cb(display, observe, LV_EVENT_INVALIDATE_AREA, &invalidations);
    tick(33);
    EXPECT_EQ(invalidations, 0u);
    const auto original = render();
    lv_obj_set_style_text_color(button, lv_color_hex(0xff8040), 0);
    const auto recolored = render();
    EXPECT_NE(original, recolored);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x203040), 0);
    EXPECT_NE(recolored, render());
    lv_display_remove_event_cb_with_user_data(display, observe, &invalidations);
}

TEST_F(FaceWidget, PreservesOuterLabelsAndPartialRedraws) {
    lv_obj_t* top = lv_label_create(button);
    lv_label_set_text(top, "Companion");
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t* bottom = lv_label_create(button);
    lv_label_set_text(bottom, "Worried");
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    create("doodle", "worried");
    tick(1000);
    const auto complete = render();
    lv_area_t top_bounds, bottom_bounds, face_bounds;
    lv_obj_get_coords(top, &top_bounds);
    lv_obj_get_coords(bottom, &bottom_bounds);
    lv_obj_get_coords(lv_obj_get_child(button, 2), &face_bounds);
    EXPECT_GT(face_bounds.y1, top_bounds.y2);
    EXPECT_LT(face_bounds.y2, bottom_bounds.y1);
    lv_area_t partial = {20, 45, 80, 95};
    lv_obj_invalidate_area(button, &partial);
    EXPECT_EQ(complete, render());
}

TEST_F(FaceWidget, AnimatedRegionsMatchFullRedrawsAndReducePixelTransfers) {
    for (const auto& size : {std::pair<int, int>{176, 144}, {75, 144}, {170, 45}}) {
        lv_obj_set_size(button, size.first, size.second);
        for (const char* style : {"robot", "doodle"}) {
            SCOPED_TRACE(std::string(style) + " " + std::to_string(size.first) + "x" + std::to_string(size.second));
            create(style);
            render();
            auto& state = *reinterpret_cast<FaceState*>(runtime.data);
            const auto compare_full = [&]() {
                const auto partial = render();
                lv_obj_invalidate(state.object);
                EXPECT_EQ(partial, render());
            };
            for (const char* mood : face_emotions) {
                SCOPED_TRACE(mood);
                emotion(mood);
                compare_full();
                for (unsigned frame = 0; frame < (static_face ? 1u : 29u); ++frame) {
                    tick(33);
                    compare_full();
                }
            }
            if (static_face) continue;
            emotion("content");
            tick(1000);
            render();
            state.next_blink = state.next_gaze = lv_tick_get() + 100000;
            state.gaze_start = lv_tick_get();
            state.gaze_from_x = state.gaze_x;
            state.gaze_from_y = state.gaze_y;
            state.gaze_to_x = state.gaze_x > 0 ? -7.5f : 7.5f;
            state.gaze_to_y = state.gaze_y > 0 ? -4.5f : 4.5f;
            for (unsigned frame = 0; frame < 13; ++frame) {
                tick(33);
                flushed_pixels = 0;
                const auto partial = render();
                EXPECT_GT(flushed_pixels, 0u);
                EXPECT_LT(flushed_pixels, static_cast<uint64_t>(size.first * size.second) / 2);
                lv_obj_invalidate(state.object);
                EXPECT_EQ(partial, render());
            }
            state.next_blink = lv_tick_get();
            for (unsigned frame = 0; frame < 9; ++frame) {
                tick(33);
                compare_full();
            }
        }
    }
}

TEST_F(FaceWidget, PortraitFullScreenMotionAvoidsFullWidgetTransfers) {
    if (static_face) GTEST_SKIP() << "Motion requires animations";
    lv_display_set_resolution(display, 720, 1280);
    lv_display_set_user_data(display, &flushed_pixels);
    lv_display_set_flush_cb(display, [](lv_display_t* output, const lv_area_t* area, uint8_t*) {
        auto* count = static_cast<uint64_t*>(lv_display_get_user_data(output));
        *count += lv_area_get_size(area);
        lv_display_flush_ready(output);
    });
    lv_obj_set_size(button, 703, 1264);
    for (const char* style : {"robot", "doodle"}) {
        SCOPED_TRACE(style);
        create(style);
        render();
        auto& state = *reinterpret_cast<FaceState*>(runtime.data);
        state.next_blink = state.next_gaze = lv_tick_get() + 100000;
        state.gaze_start = lv_tick_get();
        state.gaze_to_x = 7.5f;
        state.gaze_to_y = -4.5f;
        uint64_t maximum_pixels = 0;
        for (unsigned frame = 0; frame < 13; ++frame) {
            tick(33);
            flushed_pixels = 0;
            render();
            EXPECT_GT(flushed_pixels, 0u);
            EXPECT_LT(flushed_pixels, 703u * 1264u / 2);
            if (flushed_pixels > maximum_pixels) maximum_pixels = flushed_pixels;
        }
        RecordProperty(std::string(style) + "_maximum_gaze_pixels", std::to_string(maximum_pixels));
    }
}

TEST_F(FaceWidget, FitsSmallAndWideButtonsAndReleasesObjectsAcrossRecreation) {
    create("doodle");
    render();
    lv_mem_monitor_t before, after;
    lv_mem_monitor(&before);
    for (const auto& size : {std::pair<int, int>{20, 20}, {170, 45}, {35, 140}, {176, 144}}) {
        lv_obj_set_size(button, size.first, size.second);
        create("doodle", "surprised");
        tick(1000);
        render();
        EXPECT_FALSE(lv_obj_has_flag(lv_obj_get_child(button, 0), LV_OBJ_FLAG_CLICKABLE));
    }
    lv_mem_monitor(&after);
    EXPECT_EQ(before.used_cnt, after.used_cnt);
    EXPECT_EQ(before.free_size, after.free_size);
    lv_obj_delete(button);
    button = nullptr;
    tick(33);
}

TEST_F(FaceWidget, RejectsInvalidConfigurationAndDescribesEmotionSources) {
    JsonObject settings = json.to<JsonObject>();
    EXPECT_EQ(face_widget_type.validateConfig(settings), nullptr);
    for (const char* key : {"widget_face_style", "widget_face_emotion", "widget_face_liveliness"}) {
        settings[key] = "unknown";
        EXPECT_NE(face_widget_type.validateConfig(settings), nullptr);
        settings[key] = 7;
        EXPECT_NE(face_widget_type.validateConfig(settings), nullptr);
        settings.remove(key);
    }
    settings["widget_face_emotion"] = "[pad:mood]";
    EXPECT_NE(face_widget_type.validateConfig(settings), nullptr);
    settings.remove("widget_face_emotion");
    for (const char* value : {"content", "happy", "[pad:mood]", "[mqtt:desk/mood]"}) {
        settings["widget_data_binding"] = value;
        EXPECT_EQ(face_widget_type.validateConfig(settings), nullptr) << value;
    }
    for (const char* value : {"unknown", "[pad:mood", "Happy"}) {
        settings["widget_data_binding"] = value;
        EXPECT_NE(face_widget_type.validateConfig(settings), nullptr) << value;
    }
    settings["widget_data_binding"] = std::string(192, 'x');
    EXPECT_NE(face_widget_type.validateConfig(settings), nullptr);
    settings["widget_data_binding"] = 42;
    EXPECT_NE(face_widget_type.validateConfig(settings), nullptr);
    JsonDocument schema;
    JsonObject description = schema.to<JsonObject>();
    face_widget_type.describeSchema(description);
    EXPECT_EQ(description["config_fields"].size(), 4u);
    EXPECT_FALSE(face_widget_type.resolveInTick);
}

TEST_F(FaceWidget, BoundEmotionFallsBackRecoversAndDoesNotRestartTransitions) {
    ASSERT_TRUE(binding_template_register("mood", [](const char*, char* out, size_t size) {
        if (!face_report) return BINDING_RESOLVER_UNAVAILABLE;
        strlcpy(out, face_report, size);
        return BINDING_RESOLVER_RESOLVED;
    }, nullptr, {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, true}));
    json["widget_data_binding"] = "[mood:desk]";
    create("doodle", "angry");
    auto& state = *reinterpret_cast<FaceState*>(runtime.data);
    EXPECT_EQ(state.emotion, FaceEmotion::Content);
    EXPECT_TRUE(state.invalid_emotion);
    const auto poll = [&] {
        char value[BINDING_TEMPLATE_MAX_LEN];
        binding_template_resolve(config.data_binding[0], value, sizeof(value));
        face_widget_type.update(button, &config, &runtime, value);
    };
    face_report = "happy"; poll(); tick(198);
    EXPECT_EQ(state.emotion, FaceEmotion::Happy);
    const uint32_t start = state.transition_start;
    poll(); tick(1000);
    EXPECT_EQ(state.transition_start, start);
    EXPECT_EQ(state.transition_ms, 0u);
    EXPECT_FALSE(state.invalid_emotion);
    const char* invalid_reports[] = {nullptr, "", "unknown", "17", "ERR:missing", "Happy"};
    for (const char* invalid : invalid_reports) {
        face_report = invalid; poll();
        EXPECT_EQ(state.emotion, FaceEmotion::Content);
        EXPECT_TRUE(state.invalid_emotion);
    }
    face_report = "worried"; poll(); tick(1000);
    EXPECT_EQ(state.emotion, FaceEmotion::Worried);
    EXPECT_FALSE(state.invalid_emotion);
    EXPECT_FLOAT_EQ(state.pose.tilt, face_poses[3].tilt);
}

TEST_F(FaceWidget, NumericExpressionAndExplicitFallbackUseStandardBindingSemantics) {
    face_report = "2500";
    ASSERT_TRUE(binding_template_register("power", [](const char* params, char* out, size_t size) {
        if (strcmp(params, "offline") == 0) return BINDING_RESOLVER_UNAVAILABLE;
        strlcpy(out, face_report, size);
        return BINDING_RESOLVER_RESOLVED;
    }, nullptr, {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, true}));
    expr_binding_init();
    json["widget_data_binding"] = "[expr:[power:watts]>2000?\"worried\":\"relaxed\"]";
    create();
    EXPECT_EQ(reinterpret_cast<FaceState*>(runtime.data)->emotion, FaceEmotion::Worried);
    face_report = "2000";
    create();
    EXPECT_EQ(reinterpret_cast<FaceState*>(runtime.data)->emotion, FaceEmotion::Relaxed);
    json["widget_data_binding"] = "[power:offline|sleepy]";
    create();
    EXPECT_EQ(reinterpret_cast<FaceState*>(runtime.data)->emotion, FaceEmotion::Sleepy);
    EXPECT_FALSE(reinterpret_cast<FaceState*>(runtime.data)->invalid_emotion);
}

TEST_F(FaceWidget, RetargetingSequenceRendersIntermediateExpressions) {
    if (static_face) GTEST_SKIP() << "Animated review is covered by the animation-enabled profile";
    for (const char* style : {"robot", "doodle"}) {
        create(style);
        std::set<Pixels> frames;
        for (unsigned frame = 0; frame < 180; ++frame) {
            if (frame % 20 == 0 && frame < 160) emotion(face_emotions[frame / 20]);
            tick(66);
            render();
            frames.insert(pixels);
            save(std::string(style) + "_frame_" + std::to_string(frame));
        }

        EXPECT_GT(frames.size(), 40u) << style;
    }
}

TEST_F(FaceWidget, EpaperBlinkSurvivesOneSecondPollingAndEmotionsStillChange) {
    if (!HAS_LVGL_EPAPER) GTEST_SKIP() << "E-paper-only blink timing";
    for (const char* style : {"robot", "doodle"}) {
        SCOPED_TRACE(style);
        create(style);
        const auto open = render();
        auto& state = *reinterpret_cast<FaceState*>(runtime.data);
        state.next_blink = lv_tick_get();
        state.next_gaze = lv_tick_get() + 100000;
        tick(1000);
        EXPECT_EQ(open, render());
        tick(1000);
        const auto closed = render();
        EXPECT_NE(open, closed);
        EXPECT_FLOAT_EQ(state.eye_open, 0.04f);
        tick(1000);
        EXPECT_EQ(open, render());
        EXPECT_FALSE(state.blinking);
        emotion("happy");
        tick(1000);
        EXPECT_NE(open, render());
        EXPECT_EQ(state.transition_ms, 0u);
    }
}
