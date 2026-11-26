#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <vector>
#include "widgets/progress_widget.cpp"

void widget_register(const WidgetType*) {}
extern "C" void* __real_lv_malloc_core(size_t size);
static size_t fail_allocation_size;
static size_t allocation_calls;
extern "C" void* __wrap_lv_malloc_core(size_t size) {
    ++allocation_calls;
    if (fail_allocation_size && size == fail_allocation_size) return nullptr;
    return __real_lv_malloc_core(size);
}
#if HAS_LVGL_EPAPER
int display_manager_get_presentation_mode() { return 0; }
#endif

using namespace progress_widget;
static const char* bound_value;

class ProgressWidget : public testing::Test {
protected:
    static constexpr int width = 192, height = 160;
    std::array<uint8_t, width * height * 3> buffer{}, pixels{};
    lv_display_t* display = nullptr;
    lv_obj_t* button = nullptr;
    WidgetConfig config{};
    WidgetState runtime{};
    ScreenButtonConfig source{};
    JsonDocument json;
    bool created = false;

    void SetUp() override {
        static bool initialized = false;
        if (!initialized) { lv_init(); initialized = true; }
        display = lv_display_create(width, height);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
        lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size(), LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_user_data(display, this);
        lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t* area, uint8_t* data) {
            auto* self = static_cast<ProgressWidget*>(lv_display_get_user_data(d));
            const int stride = lv_draw_buf_width_to_stride(lv_area_get_width(area), LV_COLOR_FORMAT_RGB888);
            for (int y = area->y1; y <= area->y2; ++y)
                memcpy(self->pixels.data() + (y * width + area->x1) * 3,
                       data + (y - area->y1) * stride, lv_area_get_width(area) * 3);
            lv_display_flush_ready(d);
        });
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x102030), 0);
        button = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(button);
        lv_obj_set_size(button, width, height);
        lv_obj_set_style_text_color(button, lv_color_hex(0xffffff), 0);
        lv_obj_update_layout(button);
    }

    void TearDown() override {
        fail_allocation_size = 0;
        if (created) progress_widget_type.destroyUI(&runtime);
        lv_display_delete(display);
    }

    void create() {
        ASSERT_EQ(progress_widget_type.validateConfig(json.as<JsonObject>()), nullptr);
        progress_widget_type.parseConfig(json.as<JsonObject>(), config.data);
        strlcpy(config.data_binding[0], json["widget_data_binding"] | "", sizeof(config.data_binding[0]));
        progress_widget_type.createUI(button, &config, &source, nullptr, nullptr, &runtime);
        created = true;
        tick(0);
    }

    void tick(uint32_t elapsed) {
        lv_tick_inc(elapsed);
        progress_widget_type.tick(button, &config, &runtime);
    }

    State& state() { return *reinterpret_cast<State*>(runtime.data); }
    Config& settings() { return *reinterpret_cast<Config*>(config.data); }
    auto render() { lv_refr_now(display); return pixels; }
};

TEST_F(ProgressWidget, InvalidNumbersAndRecoveryDoNotShowZero) {
    json["widget_data_binding"] = "23";
    create();
    ASSERT_TRUE(state().valid);
    EXPECT_FLOAT_EQ(state().target, 5.75f);
    strlcpy(config.data_binding[0], "23junk", sizeof(config.data_binding[0]));
    tick(0);
    EXPECT_FALSE(state().valid);
    EXPECT_STREQ(state().error, "Progress unavailable");
    strlcpy(config.data_binding[0], "nan", sizeof(config.data_binding[0]));
    tick(0);
    EXPECT_FALSE(state().valid);
    strlcpy(config.data_binding[0], " 23 ", sizeof(config.data_binding[0]));
    tick(0);
    EXPECT_TRUE(state().valid);
    EXPECT_FLOAT_EQ(state().target, 5.75f);
    EXPECT_EQ(state().motion, nullptr);
}

TEST_F(ProgressWidget, RepeatedHideShowSnapsAndDestroyRemovesCallbacks) {
    json["widget_data_binding"] = "0";
    json["widget_progress_effect"] = "pop";
    create();
    const uint32_t callbacks = lv_obj_get_event_count(button);
    for (int i = 0; i < 3; ++i) {
        strlcpy(config.data_binding[0], "60", sizeof(config.data_binding[0]));
        tick(0);
        progress_widget_type.onHide(&runtime);
        EXPECT_EQ(state().motion, nullptr);
        strlcpy(config.data_binding[0], "20", sizeof(config.data_binding[0]));
        progress_widget_type.onShow(&runtime);
        tick(0);
        EXPECT_TRUE(state().valid);
        EXPECT_EQ(state().motion, nullptr);
        EXPECT_EQ(state().target, 5);
    }
    progress_widget_type.destroyUI(&runtime);
    created = false;
    EXPECT_EQ(lv_obj_get_event_count(button) + 2, callbacks);
    EXPECT_EQ(state().slots, nullptr);
    EXPECT_EQ(state().motion, nullptr);
}

TEST_F(ProgressWidget, RejectsInvalidLiteralRangesAndCountsAtSave) {
    json["widget_progress_min"] = 2.5;
    json["widget_progress_max"] = 2.5;
    EXPECT_NE(progress_widget_type.validateConfig(json.as<JsonObject>()), nullptr);
    json["widget_progress_max"] = 5.25;
    json["widget_progress_units"] = "2.5";
    EXPECT_NE(progress_widget_type.validateConfig(json.as<JsonObject>()), nullptr);
    json["widget_progress_units"] = 5;
    EXPECT_EQ(progress_widget_type.validateConfig(json.as<JsonObject>()), nullptr);
    create();
    EXPECT_STREQ(settings().minimum, "2.5");
    EXPECT_STREQ(settings().maximum, "5.25");
    EXPECT_EQ(state().count, 5);
}

TEST_F(ProgressWidget, CatalogDescribesBindableInputsAndGeometryLimits) {
    JsonDocument schema;
    JsonObject object = schema.to<JsonObject>();
    progress_widget_type.describeSchema(object);
    JsonArray fields = object["config_fields"];
    bool source = false, units = false, gap = false, spacing = false;
    for (JsonObject field : fields) {
        const char* name = field["name"] | "";
        if (!strcmp(name, "widget_data_binding")) source = field["bindable"] | false;
        if (!strcmp(name, "widget_progress_units"))
            units = (field["bindable"] | false) && field["minimum"] == 1 &&
                field["maximum"] == candidate_limit;
        if (!strcmp(name, "widget_progress_gap_x"))
            gap = field["minimum"] == 0 && field["maximum"] == 256;
        if (!strcmp(name, "widget_progress_match_spacing"))
            spacing = !strcmp(field["type"] | "", "boolean") && field["default"] == true;
    }
    EXPECT_TRUE(source);
    EXPECT_TRUE(units);
    EXPECT_TRUE(gap);
    EXPECT_TRUE(spacing);
}

TEST_F(ProgressWidget, RangeAndCountChangeWithoutCurrentChange) {
    json["widget_data_binding"] = "50";
    json["widget_progress_units"] = "365";
    create();
    ASSERT_TRUE(state().valid);
    EXPECT_EQ(state().count, 365);
    strlcpy(settings().units, "366", sizeof(settings().units));
    strlcpy(settings().maximum, "200", sizeof(settings().maximum));
    tick(0);
    ASSERT_TRUE(state().valid);
    EXPECT_EQ(state().count, 366);
    EXPECT_FLOAT_EQ(state().target, 91.5f);
}

TEST_F(ProgressWidget, UnmatchedAutomaticSpacingSpreadsWithoutChangingUnitSize) {
    json["widget_data_binding"] = "100";
    json["widget_progress_units"] = "100";
    json["widget_progress_columns"] = 5;
    create();
    ASSERT_TRUE(state().valid);
    EXPECT_TRUE(settings().match_spacing);
    const int cell_width = state().cell_w;
    const int cell_height = state().cell_h;
    const int old_gap_x = state().gap_x;
    const int old_gap_y = state().gap_y;
    const auto matched = render();
    settings().match_spacing = false;
    state().layout_dirty = true;
    tick(0);
    ASSERT_TRUE(state().valid);
    EXPECT_EQ(state().cell_w, cell_width);
    EXPECT_EQ(state().cell_h, cell_height);
    EXPECT_GT(state().gap_x, old_gap_x);
    EXPECT_GE(state().gap_y, old_gap_y);
    lv_obj_invalidate(state().object);
    EXPECT_NE(render(), matched);
    EXPECT_LE(state().left, 2);

    settings().columns = 20;
    settings().match_spacing = true;
    state().layout_dirty = true;
    tick(0);
    const int matched_y = state().gap_y;
    settings().match_spacing = false;
    state().layout_dirty = true;
    tick(0);
    ASSERT_TRUE(state().valid);
    EXPECT_GT(state().gap_y, matched_y);
    EXPECT_LE(state().top, 2);
}

TEST_F(ProgressWidget, NumericAndSingleUnitSpacingRemainValid) {
    json["widget_progress_units"] = "10";
    json["widget_progress_columns"] = 2;
    json["widget_progress_gap_x"] = 3;
    json["widget_progress_match_spacing"] = false;
    create();
    EXPECT_FALSE(settings().match_spacing);
    const int auto_y = state().gap_y;
    const int width_before = state().cell_w;
    settings().match_spacing = false;
    state().layout_dirty = true;
    tick(0);
    ASSERT_TRUE(state().valid);
    EXPECT_EQ(state().gap_x, 3);
    EXPECT_EQ(state().gap_y, auto_y);
    EXPECT_EQ(state().cell_w, width_before);

    strlcpy(settings().units, "1", sizeof(settings().units));
    settings().columns = 1;
    settings().gap_x = -1;
    state().layout_dirty = true;
    tick(0);
    EXPECT_TRUE(state().valid);
    EXPECT_EQ(state().count, 1);
}

TEST_F(ProgressWidget, PartialRowsHaveOneUniqueSlotForEveryOriginAndOrder) {
    json["widget_progress_units"] = "17";
    json["widget_progress_columns"] = 6;
    create();
    for (unsigned o = 0; o < 4; ++o) {
        settings().origin = static_cast<Origin>(o);
        for (unsigned order = 0; order < 10; ++order) {
            settings().order = static_cast<Order>(order);
            ASSERT_TRUE(make_order(&state()));
            std::set<uint16_t> slots(state().slots, state().slots + state().count);
            EXPECT_EQ(slots.size(), state().count) << o << " " << order;
            EXPECT_EQ(*slots.begin(), 0);
            EXPECT_EQ(*slots.rbegin(), state().count - 1);
            if (order == static_cast<unsigned>(Order::Random)) {
                settings().seed = 0;
                ASSERT_TRUE(make_order(&state()));
                const std::vector<uint16_t> before(state().slots, state().slots + state().count);
                ASSERT_TRUE(make_order(&state()));
                EXPECT_EQ(before, std::vector<uint16_t>(state().slots, state().slots + state().count));
            }
        }
    }
}

TEST_F(ProgressWidget, ReversalAndUnchangedTargetsKeepMotionContinuous) {
    json["widget_data_binding"] = "0";
    json["widget_progress_units"] = "10";
    json["widget_progress_effect"] = "pop";
    create();
    strlcpy(config.data_binding[0], "10", sizeof(config.data_binding[0]));
    tick(0);
    if (DISPLAY_DISABLE_ANIMATIONS || HAS_LVGL_EPAPER) {
        EXPECT_EQ(state().motion, nullptr);
        EXPECT_FLOAT_EQ(state().target, 1);
        return;
    }
    ASSERT_NE(state().motion, nullptr);
    tick(100);
    const Sample before = sample(state().motion[0], lv_tick_get(), settings().duration);
    const uint32_t started = state().motion[0].started;
    tick(0);
    EXPECT_EQ(state().motion[0].started, started);
    strlcpy(config.data_binding[0], "0", sizeof(config.data_binding[0]));
    tick(0);
    const Sample after = sample(state().motion[0], lv_tick_get(), settings().duration);
    EXPECT_FLOAT_EQ(before.value, after.value);
    EXPECT_FLOAT_EQ(before.phase, after.phase);
}

TEST_F(ProgressWidget, HiddenUnitsDoNotMoveAndInstantDoesNotAllocateMotion) {
    json["widget_data_binding"] = "0";
    json["widget_progress_inactive_style"] = "hidden";
    json["widget_anim_ms"] = 0;
    json["widget_progress_stagger_ms"] = 900;
    create();
    const auto empty = render();
    const std::vector<uint16_t> slots(state().slots, state().slots + state().count);
    strlcpy(config.data_binding[0], "100", sizeof(config.data_binding[0]));
    tick(0);
    EXPECT_NE(render(), empty);
    EXPECT_EQ(slots, std::vector<uint16_t>(state().slots, state().slots + state().count));
    EXPECT_EQ(state().motion, nullptr);
}

TEST_F(ProgressWidget, FadeRestoresNonuniformBackgroundWithoutErasingIt) {
    for (int x : {0, width / 2}) {
        lv_obj_t* background = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(background);
        lv_obj_set_pos(background, x, 0);
        lv_obj_set_size(background, width / 2, height);
        lv_obj_set_style_bg_color(background, lv_color_hex(x ? 0x1122dd : 0xcc3311), 0);
        lv_obj_set_style_bg_opa(background, LV_OPA_COVER, 0);
    }
    lv_obj_move_foreground(button);
    json["widget_progress_effect"] = "fade";
    json["widget_progress_inactive_style"] = "hidden";
    create();
    const auto background = render();
    strlcpy(config.data_binding[0], "100", sizeof(config.data_binding[0]));
    tick(0);
    tick(400);
    EXPECT_NE(render(), background);
    strlcpy(config.data_binding[0], "0", sizeof(config.data_binding[0]));
    tick(0);
    tick(400);
    EXPECT_EQ(render(), background);
}

TEST_F(ProgressWidget, DensityFailureIsVisibleAndRecovers) {
    json["widget_progress_units"] = "366";
    json["widget_progress_columns"] = 30;
    json["widget_progress_gap_x"] = 10;
    create();
    EXPECT_FALSE(state().valid);
    EXPECT_STREQ(state().error, "Insufficient widget space");
    settings().gap_x = 0;
    state().layout_dirty = true;
    tick(0);
    EXPECT_TRUE(state().valid);
}

TEST_F(ProgressWidget, CheckedBindingsColorsAndResourceVisits) {
    ASSERT_TRUE(binding_template_register("progress_test", [](const char*, char* out, size_t size) {
        if (!bound_value) return BINDING_RESOLVER_UNAVAILABLE;
        strlcpy(out, bound_value, size);
        return BINDING_RESOLVER_RESOLVED;
    }, nullptr, {1, 1, 1, 1, BINDING_VALIDATION_STANDARD, true}));
    bound_value = "40";
    json["widget_data_binding"] = "[progress_test:value]";
    json["widget_progress_max"] = "[progress_test:value]";
    json["widget_progress_inactive_style"] = "hidden";
    json["widget_progress_inactive_color"] = "[progress_test:color]";
    create();
    ASSERT_TRUE(state().valid);
    EXPECT_EQ(state().target, 25);
    unsigned visited = 0;
    progress_widget_type.visitResources(config.data,
        [](WidgetResourceKind kind, const char*, uint8_t, const char* text, void* context) {
            if (kind == WidgetResourceKind::Binding && strstr(text, "[progress_test:"))
                ++*static_cast<unsigned*>(context);
        }, &visited);
    EXPECT_EQ(visited, 2u);
    bound_value = nullptr;
    tick(0);
    EXPECT_FALSE(state().valid);
    bound_value = "40";
    tick(0);
    EXPECT_TRUE(state().valid);
    settings().inactive = Inactive::Filled;
    tick(0);
    EXPECT_FALSE(state().valid);
    bound_value = "#abcdef";
    strlcpy(config.data_binding[0], "20", sizeof(config.data_binding[0]));
    strlcpy(settings().maximum, "100", sizeof(settings().maximum));
    tick(0);
    EXPECT_TRUE(state().valid);
}

TEST_F(ProgressWidget, WholeUnitsNeverRoundAnIncompleteValueUp) {
    json["widget_progress_partial"] = "whole";
    json["widget_data_binding"] = "12.999999999999";
    json["widget_progress_units"] = "100";
    create();
    ASSERT_TRUE(state().valid);
    EXPECT_EQ(target_for(state(), 12, state().target), 0);
    strlcpy(config.data_binding[0], "13", sizeof(config.data_binding[0]));
    tick(0);
    EXPECT_EQ(target_for(state(), 12, state().target), 1);
}

TEST_F(ProgressWidget, AllocationFailureReportsUnavailableAndRecovers) {
    json["widget_progress_units"] = "60";
    fail_allocation_size = 60 * sizeof(uint16_t);
    create();
    EXPECT_FALSE(state().valid);
    EXPECT_STREQ(state().error, "Progress allocation failed");
    EXPECT_EQ(state().slots, nullptr);
    fail_allocation_size = 0;
    tick(0);
    ASSERT_TRUE(state().valid);
    EXPECT_NE(state().slots, nullptr);
    if (!DISPLAY_DISABLE_ANIMATIONS && !HAS_LVGL_EPAPER) {
        fail_allocation_size = 60 * sizeof(Motion);
        strlcpy(config.data_binding[0], "10", sizeof(config.data_binding[0]));
        tick(0);
        EXPECT_FALSE(state().valid);
        EXPECT_STREQ(state().error, "Progress animation allocation failed");
        fail_allocation_size = 0;
        tick(0);
        EXPECT_TRUE(state().valid);
        EXPECT_EQ(state().motion, nullptr);
    }
}

TEST_F(ProgressWidget, ResizeFailureReleasesOldOrderAndRetriesWithoutStaleSlots) {
    json["widget_progress_units"] = "60";
    create();
    ASSERT_TRUE(state().valid);
    strlcpy(settings().units, "61", sizeof(settings().units));
    fail_allocation_size = 61 * sizeof(SortSlot);
    tick(0);
    EXPECT_FALSE(state().valid);
    EXPECT_EQ(state().slots, nullptr);
    EXPECT_EQ(state().motion, nullptr);
    fail_allocation_size = 0;
    tick(0);
    ASSERT_TRUE(state().valid);
    EXPECT_EQ(state().count, 61);
    EXPECT_EQ(std::set<uint16_t>(state().slots, state().slots + state().count).size(), 61u);
    const size_t allocations = allocation_calls;
    for (int i = 0; i < 10; ++i) tick(33);
    EXPECT_EQ(allocation_calls, allocations);
}

TEST_F(ProgressWidget, FractionalRetargetsAndClockWrapPreserveVisibleState) {
    json["widget_data_binding"] = "12.4";
    json["widget_progress_units"] = "100";
    json["widget_progress_effect"] = "flip";
    create();
    if (DISPLAY_DISABLE_ANIMATIONS || HAS_LVGL_EPAPER) return;
    lv_tick_inc(UINT32_MAX - lv_tick_get() - 50);
    strlcpy(config.data_binding[0], "12.6", sizeof(config.data_binding[0]));
    tick(0);
    ASSERT_NE(state().motion, nullptr);
    EXPECT_FLOAT_EQ(state().motion[12].phase_from, 1);
    EXPECT_FLOAT_EQ(state().motion[12].phase_to, 1);
    tick(100);
    const Sample before = sample(state().motion[12], lv_tick_get(), settings().duration);
    strlcpy(config.data_binding[0], "12.2", sizeof(config.data_binding[0]));
    tick(0);
    const Sample after = sample(state().motion[12], lv_tick_get(), settings().duration);
    EXPECT_FLOAT_EQ(before.value, after.value);
    EXPECT_FLOAT_EQ(before.phase, after.phase);
    tick(400);
    EXPECT_FLOAT_EQ(sample(state().motion[12], lv_tick_get(), settings().duration).value, 0.2f);
}

TEST_F(ProgressWidget, EffectsSettleIdenticallyAndReverseWithoutPixelJump) {
    std::array<uint8_t, width * height * 3> baseline{};
    bool first = true;
    for (const char* effect : effects) {
        if (created) { progress_widget_type.destroyUI(&runtime); created = false; }
        json.clear();
        json["widget_data_binding"] = "0";
        json["widget_progress_effect"] = effect;
        json["widget_progress_inactive_style"] = "hidden";
        json["widget_progress_partial"] = "fill";
        create();
        strlcpy(config.data_binding[0], "37.5", sizeof(config.data_binding[0]));
        tick(0);
        tick(400);
        const auto settled = render();
        if (first) { baseline = settled; first = false; }
        else EXPECT_EQ(settled, baseline) << effect;
        strlcpy(config.data_binding[0], "75", sizeof(config.data_binding[0]));
        tick(0);
        tick(100);
        const auto before = render();
        strlcpy(config.data_binding[0], "30", sizeof(config.data_binding[0]));
        tick(0);
        if (!DISPLAY_DISABLE_ANIMATIONS && !HAS_LVGL_EPAPER && strcmp(effect, "none")) {
            EXPECT_EQ(render(), before) << effect;
        }
    }
}

TEST_F(ProgressWidget, PopOvershootsSettledSizeWithinAvailableSpacing) {
    if (DISPLAY_DISABLE_ANIMATIONS || HAS_LVGL_EPAPER) return;
    json["widget_data_binding"] = "4";
    json["widget_progress_max"] = 9;
    json["widget_progress_units"] = 9;
    json["widget_progress_columns"] = 3;
    json["widget_progress_gap_x"] = 16;
    json["widget_progress_gap_y"] = 16;
    json["widget_progress_shape"] = "block";
    json["widget_progress_inactive_style"] = "hidden";
    json["widget_progress_effect"] = "pop";
    create();
    const int x = state().left + state().cell_w + state().gap_x;
    const int y = state().top + state().cell_h + state().gap_y + state().cell_h / 2;
    auto activeWidth = [&]() {
        const auto image = render();
        int first = width, last = -1;
        for (int column = x - 7; column < x + state().cell_w + 7; ++column) {
            const size_t pixel = (y * width + column) * 3;
            if (image[pixel] == 255 && image[pixel + 1] == 255 && image[pixel + 2] == 255) {
                first = std::min(first, column);
                last = column;
            }
        }
        return last - first + 1;
    };
    strlcpy(config.data_binding[0], "5", sizeof(config.data_binding[0]));
    tick(0);
    tick(190);
    const int onPeak = activeWidth();
    tick(200);
    const int settled = activeWidth();
    EXPECT_EQ(settled, state().cell_w);
    EXPECT_GE(onPeak, settled * 1.10f);
    EXPECT_LE(onPeak, settled * 1.17f);
    strlcpy(config.data_binding[0], "4", sizeof(config.data_binding[0]));
    tick(0);
    tick(110);
    EXPECT_EQ(activeWidth(), onPeak);
}

TEST_F(ProgressWidget, PopDoesNotPaintIntoTouchingInactiveUnits) {
    if (DISPLAY_DISABLE_ANIMATIONS || HAS_LVGL_EPAPER) return;
    json["widget_data_binding"] = "4";
    json["widget_progress_max"] = 9;
    json["widget_progress_units"] = 9;
    json["widget_progress_columns"] = 3;
    json["widget_progress_gap_x"] = 0;
    json["widget_progress_gap_y"] = 0;
    json["widget_progress_shape"] = "block";
    json["widget_progress_inactive_style"] = "hidden";
    json["widget_progress_effect"] = "pop";
    create();
    strlcpy(config.data_binding[0], "5", sizeof(config.data_binding[0]));
    tick(0);
    tick(190);
    const auto image = render();
    const int x = state().left + 2 * state().cell_w;
    const int y = state().top + state().cell_h + state().cell_h / 2;
    const size_t pixel = (y * width + x) * 3;
    EXPECT_NE(image[pixel], 255);
}

TEST_F(ProgressWidget, StaggerUsesTotalSpanAndDecreasesReverseOrder) {
    json["widget_progress_units"] = "10";
    json["widget_progress_stagger_ms"] = 500;
    json["widget_data_binding"] = "0";
    create();
    if (DISPLAY_DISABLE_ANIMATIONS || HAS_LVGL_EPAPER) return;
    strlcpy(config.data_binding[0], "100", sizeof(config.data_binding[0]));
    tick(0);
    ASSERT_NE(state().motion, nullptr);
    EXPECT_EQ(state().motion[0].delay, 0);
    EXPECT_EQ(state().motion[9].delay, 500);
    tick(900);
    strlcpy(config.data_binding[0], "0", sizeof(config.data_binding[0]));
    tick(0);
    EXPECT_EQ(state().motion[9].delay, 0);
    EXPECT_EQ(state().motion[0].delay, 500);
    tick(900);
    EXPECT_FALSE(state().motion[0].running);
}

TEST_F(ProgressWidget, NineReferenceFamiliesRenderAtRepresentativeValues) {
    struct Recipe {
        const char* name;
        const char* layout;
        const char* shape;
        const char* origin;
        int units, columns, gap_x, gap_y, rounding, inactive_size;
        float aspect;
    };
    const Recipe recipes[] = {
        {"growing_dots", "grid", "circle", "top_left", 25, 5, 6, 6, 0, 25, 1},
        {"uniform_dots", "grid", "circle", "top_left", 30, 6, 8, 8, 0, 100, 1},
        {"dense_dots", "grid", "circle", "bottom_left", 96, 12, 5, 5, 0, 100, 1},
        {"soft_waffle", "grid", "block", "bottom_left", 100, 10, 2, 2, 20, 100, 1},
        {"pixel_blocks", "grid", "block", "top_left", 96, 12, 2, 2, 0, 100, 1},
        {"capsule_matrix", "grid", "block", "bottom_left", 36, 6, 5, 7, 50, 100, 1.83f},
        {"rounded_strip", "horizontal", "block", "top_left", 10, 10, 4, 4, 50, 100, 1},
        {"fine_strip", "horizontal", "block", "top_left", 16, 16, 3, 3, 0, 100, 1},
        {"vertical_stack", "vertical", "block", "bottom_left", 10, 1, 6, 4, 50, 100, 1}
    };
    if (HAS_LVGL_EPAPER) {
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_color(button, lv_color_hex(0), 0);
    }
    for (const Recipe& recipe : recipes) {
        if (created) { progress_widget_type.destroyUI(&runtime); created = false; }
        json.clear();
        json["widget_progress_layout"] = recipe.layout;
        json["widget_progress_shape"] = recipe.shape;
        json["widget_progress_origin"] = recipe.origin;
        json["widget_progress_units"] = recipe.units;
        json["widget_progress_columns"] = recipe.columns;
        json["widget_progress_gap_x"] = recipe.gap_x;
        json["widget_progress_gap_y"] = recipe.gap_y;
        json["widget_progress_rounding"] = recipe.rounding;
        json["widget_progress_inactive_size"] = recipe.inactive_size;
        json["widget_progress_aspect"] = recipe.aspect;
        json["widget_progress_partial"] = recipe.inactive_size == 25 ? "size" : "fill";
        json["widget_anim_ms"] = 0;
        create();
        ASSERT_TRUE(state().valid) << recipe.name;
        auto empty = render();
        for (const char* value : {"0", "12.5", "37.5", "75", "100"}) {
            strlcpy(config.data_binding[0], value, sizeof(config.data_binding[0]));
            tick(0);
            const auto image = render();
            if (!strcmp(value, "100")) {
                EXPECT_NE(image, empty) << recipe.name;
            }
            const char* directory = getenv("PROGRESS_WIDGET_PREVIEW_DIR");
            if (directory) {
                char path[512];
                snprintf(path, sizeof(path), "%s/%s-%s-%s.ppm", directory, recipe.name,
                         value, HAS_LVGL_EPAPER ? "mono" : "color");
                std::ofstream file(path, std::ios::binary);
                ASSERT_TRUE(file.is_open()) << path;
                file << "P6\n" << width << " " << height << "\n255\n";
                for (size_t i = 0; i < image.size(); i += 3) {
                    const unsigned intensity = 299u * image[i + 2] + 587u * image[i + 1] + 114u * image[i];
                    const char mono = intensity >= 128000 ? static_cast<char>(255) : 0;
                    const char rgb[] = {
                        HAS_LVGL_EPAPER ? mono : static_cast<char>(image[i + 2]),
                        HAS_LVGL_EPAPER ? mono : static_cast<char>(image[i + 1]),
                        HAS_LVGL_EPAPER ? mono : static_cast<char>(image[i])
                    };
                    file.write(rgb, sizeof(rgb));
                }
                ASSERT_TRUE(file.good()) << path;
            }
        }
    }
}

TEST_F(ProgressWidget, MinuteScenarioAddsRemovesAndResetsAllSixtyUnits) {
    for (const char* effect : effects) {
        if (created) { progress_widget_type.destroyUI(&runtime); created = false; }
        json.clear();
        json["widget_progress_units"] = 60;
        json["widget_progress_max"] = 60;
        json["widget_progress_columns"] = 10;
        json["widget_progress_effect"] = effect;
        json["widget_progress_stagger_ms"] = 300;
        json["widget_anim_ms"] = 450;
        json["widget_progress_inactive_style"] = "hidden";
        create();
        for (int value = 1; value <= 60; ++value) {
            snprintf(config.data_binding[0], sizeof(config.data_binding[0]), "%d", value);
            tick(0);
            if (state().motion) { EXPECT_EQ(state().motion[value - 1].delay, 0); }
            tick(1000);
            EXPECT_DOUBLE_EQ(state().target, value);
            render();
        }
        const auto full = render();
        tick(1000);
        EXPECT_EQ(render(), full);
        for (int value = 59; value >= 0; --value) {
            snprintf(config.data_binding[0], sizeof(config.data_binding[0]), "%d", value);
            tick(0);
            if (state().motion) { EXPECT_EQ(state().motion[value].delay, 0); }
            tick(1000);
            EXPECT_DOUBLE_EQ(state().target, value);
            render();
        }
        strlcpy(config.data_binding[0], "60", sizeof(config.data_binding[0]));
        tick(0);
        tick(1000);
        strlcpy(config.data_binding[0], "0", sizeof(config.data_binding[0]));
        tick(0);
        if (state().motion) {
            EXPECT_EQ(state().motion[59].delay, 0);
            EXPECT_EQ(state().motion[0].delay, 300);
        }
        tick(750);
        if (state().motion) { EXPECT_FALSE(state().motion[0].running); }
    }
}

TEST_F(ProgressWidget, PrintRepresentativeHostTimingAndMemory) {
    json["widget_data_binding"] = "50";
    json["widget_progress_units"] = "60";
    create();
    printf("Progress host sizes: config=%zu state=%zu motion=%zu order=%zu bytes/unit\n",
           sizeof(Config), sizeof(State), sizeof(Motion), sizeof(uint16_t));
    for (unsigned count : {60u, 366u, 1024u}) {
        snprintf(settings().units, sizeof(settings().units), "%u", count);
        settings().gap_x = settings().gap_y = 0;
        state().layout_dirty = true;
        for (Shape shape : {Shape::Circle, Shape::Block}) {
            settings().shape = shape;
            settings().effect = Effect::Pop;
            state().layout_dirty = true;
            tick(0);
            ASSERT_TRUE(state().valid);
            long long max_draw = 0, max_update = 0;
            for (int i = 0; i < 10; ++i) {
                strlcpy(config.data_binding[0], i % 2 ? "20" : "80", sizeof(config.data_binding[0]));
                auto start = std::chrono::steady_clock::now();
                tick(33);
                max_update = std::max(max_update, static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start).count()));
                lv_obj_invalidate(state().object);
                start = std::chrono::steady_clock::now();
                render();
                max_draw = std::max(max_draw, static_cast<long long>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start).count()));
            }
            printf("Progress host %u %s: max update %lld us, max draw %lld us\n", count,
                   shape == Shape::Circle ? "circles" : "blocks", max_update, max_draw);
        }
    }
}
