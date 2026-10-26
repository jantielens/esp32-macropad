#include <gtest/gtest.h>

#include "widgets/slider_numeric.h"
#include "widgets/slider_widget.cpp"

static unsigned g_dispatches;
static uint8_t g_action_count;
static char g_action_values[MAX_BUTTON_ACTIONS][32];
static char g_dispatched_value[32];
static const char* g_binding_value = "0";
static ActionResult g_dispatch_result = ACTION_COMPLETE;
static lv_obj_t* g_delete_on_dispatch = nullptr;

static const WidgetType* g_registered_slider = nullptr;
void widget_register(const WidgetType* type) { g_registered_slider = type; }

ActionResult action_list_dispatch_result(const ButtonAction* actions, uint8_t count,
                                         const char*, ActionContinuationOwner) {
    if (count) {
        ++g_dispatches;
        g_action_count = count;
        for (uint8_t index = 0; index < count; ++index)
            strlcpy(g_action_values[index], actions[index].payload.mqtt.mqtt_payload, sizeof(g_action_values[index]));
        strlcpy(g_dispatched_value, actions[0].payload.mqtt.mqtt_payload,
                sizeof(g_dispatched_value));
    }
    if (g_delete_on_dispatch) {
        lv_obj_t* target = g_delete_on_dispatch;
        g_delete_on_dispatch = nullptr;
        lv_obj_delete(target);
    }
    return g_dispatch_result;
}

void action_parse(const JsonObject& action, ButtonAction& output) {
    memset(&output, 0, sizeof(output));
    strlcpy(output.type, action["type"] | "", sizeof(output.type));
    strlcpy(output.payload.mqtt.mqtt_payload, action["payload"] | "",
            sizeof(output.payload.mqtt.mqtt_payload));
}

static char* test_mqtt_value_field(ButtonAction& action, size_t* size) {
    *size = sizeof(action.payload.mqtt.mqtt_payload);
    return action.payload.mqtt.mqtt_payload;
}

static bool test_mqtt_fields(ButtonAction& action, ActionFieldVisitor visitor, void* context) {
    return action_visit_field(visitor, context, "payload", action.payload.mqtt.mqtt_payload);
}

static const ActionTypeDef g_mqtt_type = {
    "mqtt", nullptr, nullptr, nullptr, test_mqtt_fields, nullptr
};

const ActionTypeDef* action_type_find(const char* type) {
    return type && strcmp(type, "mqtt") == 0 ? &g_mqtt_type : nullptr;
}

bool action_type_substitute_token(const ActionTypeDef* type,
                                  ButtonAction& action, const char* name, const char* value) {
    if (type != &g_mqtt_type) return false;
    size_t field_size = 0;
    char* field = test_mqtt_value_field(action, &field_size);
    char* token = strstr(field, name);
    if (!token) return true;
    const size_t tail = strlen(token + 7);
    const size_t replacement = strlen(value);
    if (static_cast<size_t>(token - field) + replacement + tail >= field_size)
        return false;
    memmove(token + replacement, token + 7, tail + 1);
    memcpy(token, value, replacement);
    return true;
}

struct SliderPointerInput {
    lv_point_t point{};
    bool pressed = false;
};

static void read_slider_pointer(lv_indev_t* indev, lv_indev_data_t* data) {
    const auto* input = static_cast<const SliderPointerInput*>(lv_indev_get_user_data(indev));
    data->point = input->point;
    data->state = input->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static const lv_image_dsc_t* slider_test_icon_source() {
    static const uint8_t pixels[24 * 24 * 2] = {};
    static lv_image_dsc_t image{};
    static bool initialized = false;
    if (!initialized) {
        image.header.magic = LV_IMAGE_HEADER_MAGIC;
        image.header.cf = LV_COLOR_FORMAT_RGB565;
        image.header.w = 24;
        image.header.h = 24;
        image.header.stride = 24 * 2;
        image.data_size = sizeof(pixels);
        image.data = pixels;
        initialized = true;
    }
    return &image;
}

bool binding_template_resolve_checked(const char*, char* output, size_t size) {
    strlcpy(output, g_binding_value, size);
    return true;
}

class SliderNativeWidget : public testing::Test {
protected:
    lv_display_t* display = nullptr;
    lv_indev_t* indev = nullptr;
    lv_obj_t* button = nullptr;
    unsigned feedback_count = 0;
    SliderPointerInput pointer{};
    WidgetConfig config{};
    WidgetState runtime{};
    ScreenButtonConfig source{};
    JsonDocument json;

    void SetUp() override {
        static bool initialized = false;
        if (!initialized) { lv_init(); initialized = true; }
        display = lv_display_create(320, 240);
        lv_display_set_default(display);
        indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(indev, display);
        lv_indev_set_user_data(indev, &pointer);
        lv_indev_set_read_cb(indev, read_slider_pointer);
        button = lv_obj_create(lv_screen_active());
        lv_obj_set_size(button, 200, 180);
        lv_obj_add_event_cb(button, [](lv_event_t* event) {
            const auto* feedback = static_cast<const lv_event_code_t*>(lv_event_get_param(event));
            if (feedback && *feedback == LV_EVENT_RELEASED)
                ++*static_cast<unsigned*>(lv_event_get_user_data(event));
        }, LV_EVENT_VALUE_CHANGED, &feedback_count);
        g_dispatches = 0;
        g_dispatched_value[0] = '\0';
        g_binding_value = "0";
        g_dispatch_result = ACTION_COMPLETE;
        g_delete_on_dispatch = nullptr;
        pointer = {};
        JsonObject object = json.to<JsonObject>();
        source.action_count = 1;
        strlcpy(source.actions[0].type, "mqtt", sizeof(source.actions[0].type));
        strlcpy(source.actions[0].payload.mqtt.mqtt_payload, "{value}", sizeof(source.actions[0].payload.mqtt.mqtt_payload));
        slider_parse(object, config.data);
        strlcpy(config.type, "slider", sizeof(config.type));
        slider_create(button, &config, &source, nullptr, &UI_SCALE_TABLE[0],
                      nullptr, nullptr, &runtime);
        lv_obj_update_layout(button);
    }

    void TearDown() override {
        g_delete_on_dispatch = nullptr;
        slider_destroy(&runtime);
        lv_indev_delete(indev);
        lv_display_delete(display);
    }

    SliderState& state() { return *reinterpret_cast<SliderState*>(runtime.data); }

    void begin(uint16_t position) {
        lv_obj_send_event(state().control, LV_EVENT_PRESSED, nullptr);
        lv_slider_set_value(state().control, position, LV_ANIM_OFF);
        lv_obj_send_event(state().control, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    void release() { lv_obj_send_event(state().control, LV_EVENT_RELEASED, nullptr); }
    void readPointer(lv_point_t point, bool pressed) {
        pointer.point = point;
        pointer.pressed = pressed;
        lv_tick_inc(10);
        lv_indev_read(indev);
    }
    void createLayout(uint8_t axis) {
        slider_destroy(&runtime);
        lv_obj_clean(button);
        runtime = WidgetState{};
        reinterpret_cast<SliderConfig*>(config.data)->axis = axis;
        lv_obj_t* top = lv_label_create(button);
        lv_label_set_text(top, "Top label");
        lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_t* bottom = lv_label_create(button);
        lv_label_set_text(bottom, "Bottom label");
        lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_t* center = lv_label_create(button);
        lv_label_set_text(center, "Center label");
        lv_obj_align(center, LV_ALIGN_CENTER, 0, 0);
        lv_obj_t* icon = lv_image_create(button);
        lv_image_set_src(icon, slider_test_icon_source());
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
        slider_create(button, &config, &source, nullptr, &UI_SCALE_TABLE[0],
                      icon, center, &runtime);
        lv_obj_update_layout(button);
        slider_layout(&state());
    }
};

TEST_F(SliderNativeWidget, AutomaticSizingUsesFullLengthAndShrinksToFitBothAxes) {
    auto* settings = reinterpret_cast<SliderConfig*>(config.data);
    for (uint8_t axis : {0, 1}) {
        settings->axis = axis;
        for (const auto& size : {lv_point_t{450, 300}, lv_point_t{140, 70},
                                 lv_point_t{80, 40}, lv_point_t{200, 40}}) {
            lv_obj_set_style_pad_all(button, 0, 0);
            lv_obj_set_style_border_width(button, 0, 0);
            lv_obj_set_size(button, axis == 0 ? size.x : size.y,
                            axis == 0 ? size.y : size.x);
            slider_layout(&state());
            const int expected_knob = std::min(50, std::min(size.y * 3 / 5, size.x / 4));
            const int expected_track = expected_knob * 2 / 5;
            EXPECT_TRUE(state().layout_usable);
            EXPECT_EQ(axis == 0 ? lv_obj_get_width(state().control) : lv_obj_get_height(state().control),
                      size.x - expected_knob);
            EXPECT_EQ(axis == 0 ? lv_obj_get_height(state().control) : lv_obj_get_width(state().control),
                      expected_track);
            EXPECT_EQ(lv_obj_get_style_pad_left(state().control, LV_PART_KNOB),
                      (expected_knob - expected_track) / 2);
            EXPECT_EQ(lv_obj_get_x(state().control),
                      (lv_obj_get_content_width(button) - lv_obj_get_width(state().control)) / 2);
            EXPECT_EQ(lv_obj_get_y(state().control),
                      (lv_obj_get_content_height(button) - lv_obj_get_height(state().control)) / 2);
        }
    }
}

TEST_F(SliderNativeWidget, OuterIconDoesNotReserveHalfTheSliderLength) {
    createLayout(0);
    lv_obj_delete(state().center_label);
    state().center_label = nullptr;
    auto* settings = reinterpret_cast<SliderConfig*>(config.data);
    settings->knob_px = 20;
    settings->track_px = 4;
    slider_layout(&state());
    EXPECT_EQ(lv_obj_get_width(state().control) + 20, lv_obj_get_content_width(button) / 2);
    for (uint8_t position : {1, 2}) {
        settings->icon_position = position;
        slider_layout(&state());
        EXPECT_EQ(lv_obj_get_width(state().control) + 20, lv_obj_get_content_width(button));
    }
}

TEST_F(SliderNativeWidget, ChangedReleaseDispatchesAbsoluteValueOnce) {
    EXPECT_TRUE(state().available);
    begin(42);
    EXPECT_EQ(feedback_count, 0u);
    release();
    EXPECT_EQ(feedback_count, 1u);
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_STREQ(g_dispatched_value, "42");
    release();
    EXPECT_EQ(feedback_count, 1u);
    EXPECT_EQ(g_dispatches, 1u);
}

TEST_F(SliderNativeWidget, MissingActionKeepsLocalSliderInteractive) {
    state().action_count = 0;
    begin(37);
    release();
    EXPECT_EQ(g_dispatches, 0u);
    EXPECT_EQ(state().display_position, 37);
    EXPECT_FALSE(state().pending);
}

TEST_F(SliderNativeWidget, MissingActionBoundSliderReturnsToReportedValue) {
    strlcpy(config.data_binding[0], "[mock:value]", sizeof(config.data_binding[0]));
    slider_destroy(&runtime);
    lv_obj_clean(button);
    runtime = WidgetState{};
    auto* slider_config = reinterpret_cast<SliderConfig*>(config.data);
    ASSERT_TRUE(slider_numeric_range_init(0, 1, 0.01, &slider_config->range));
    source.action_count = 0;
    slider_create(button, &config, &source, nullptr, &UI_SCALE_TABLE[0],
                  nullptr, nullptr, &runtime);
    g_binding_value = "0.2";
    slider_tick(button, &config, &runtime);
    ASSERT_EQ(state().display_position, 20);
    begin(60);
    release();
    EXPECT_EQ(g_dispatches, 0u);
    EXPECT_EQ(state().display_position, 20);
}

TEST_F(SliderNativeWidget, PointerDragAndTrackTapDispatchOnRelease) {
    unsigned swipes = 0;
    lv_obj_add_event_cb(lv_screen_active(), [](lv_event_t* event) {
        ++*static_cast<unsigned*>(lv_event_get_user_data(event));
        lv_indev_wait_release(lv_indev_active());
    }, LV_EVENT_GESTURE, &swipes);
    lv_area_t area{};
    lv_obj_get_coords(state().control, &area);
    const int center_y = (area.y1 + area.y2) / 2;
    readPointer({area.x1 + 4, center_y}, true);
    readPointer({area.x1 + (area.x2 - area.x1) * 3 / 4, center_y}, true);
    EXPECT_TRUE(state().gesture_active);
    EXPECT_EQ(feedback_count, 0u);
    readPointer({area.x1 + (area.x2 - area.x1) * 3 / 4, center_y}, false);
    EXPECT_EQ(feedback_count, 1u);
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_GT(std::atoi(g_dispatched_value), 50);

    lv_obj_get_coords(state().control, &area);
    readPointer({area.x1 + (area.x2 - area.x1) / 3, center_y}, true);
    readPointer({area.x1 + (area.x2 - area.x1) / 3, center_y}, false);
    EXPECT_EQ(feedback_count, 2u);
    EXPECT_EQ(g_dispatches, 2u);
    EXPECT_EQ(swipes, 0u);
}

TEST_F(SliderNativeWidget, HorizontalLayoutPlacesIconAndCenterLabelBesideTrack) {
    lv_obj_set_size(button, 120, 120);
    createLayout(0);
    const auto* layout = reinterpret_cast<const SliderState*>(runtime.data);
    lv_area_t top{}, bottom{}, icon{}, center{}, control{};
    lv_obj_get_coords(layout->top_label, &top);
    lv_obj_get_coords(layout->bottom_label, &bottom);
    lv_obj_get_coords(layout->icon, &icon);
    lv_obj_get_coords(layout->center_label, &center);
    lv_obj_get_coords(layout->control, &control);
    EXPECT_FALSE(lv_obj_has_flag(layout->icon, LV_OBJ_FLAG_HIDDEN));
    EXPECT_FALSE(lv_obj_has_flag(layout->center_label, LV_OBJ_FLAG_HIDDEN));
    EXPECT_LT(top.y2, icon.y1);
    EXPECT_LT(icon.x2, center.x1);
    EXPECT_LT(center.x2, control.x1);
    EXPECT_NEAR((icon.y1 + icon.y2) / 2, (center.y1 + center.y2) / 2, 1);
    EXPECT_NEAR((center.y1 + center.y2) / 2, (control.y1 + control.y2) / 2, 1);
    EXPECT_LT(control.y2, bottom.y1);
}

TEST_F(SliderNativeWidget, VerticalLayoutReservesOuterLabelsAndPlacesContentBesideTrack) {
    createLayout(1);
    const auto* layout = reinterpret_cast<const SliderState*>(runtime.data);
    lv_area_t top{}, bottom{}, icon{}, center{}, control{};
    lv_obj_get_coords(layout->top_label, &top);
    lv_obj_get_coords(layout->bottom_label, &bottom);
    lv_obj_get_coords(layout->icon, &icon);
    lv_obj_get_coords(layout->center_label, &center);
    lv_obj_get_coords(layout->control, &control);
    EXPECT_FALSE(lv_obj_has_flag(layout->icon, LV_OBJ_FLAG_HIDDEN));
    EXPECT_FALSE(lv_obj_has_flag(layout->center_label, LV_OBJ_FLAG_HIDDEN));
    EXPECT_LT(top.y2, icon.y1);
    EXPECT_LT(icon.y2, center.y1);
    EXPECT_LT(control.x2, icon.x1);
    EXPECT_LT(std::max(control.y1, icon.y1), bottom.y1);
    EXPECT_LT(std::max(control.y2, center.y2), bottom.y1);
}

TEST_F(SliderNativeWidget, InsufficientContentHidesControlWithoutInvalidGeometry) {
    lv_obj_set_size(button, 80, 40);
    createLayout(0);
    const auto* layout = reinterpret_cast<const SliderState*>(runtime.data);
    EXPECT_FALSE(layout->layout_usable);
    EXPECT_TRUE(lv_obj_has_flag(layout->control, LV_OBJ_FLAG_HIDDEN));
    EXPECT_GE(lv_obj_get_width(layout->control), 1);
    EXPECT_GE(lv_obj_get_height(layout->control), 1);
}

TEST_F(SliderNativeWidget, ImmediateDispatchFailureRestoresGestureStart) {
    g_dispatch_result = ACTION_FAILED;
    begin(30);
    release();
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_EQ(state().display_position, 0);
    EXPECT_FALSE(state().pending);
}

TEST_F(SliderNativeWidget, ThreeActionsShareReleaseSnapshotAndAllowLiteralActions) {
    source.actions[1] = source.actions[0];
    source.actions[2] = source.actions[0];
    strlcpy(source.actions[1].payload.mqtt.mqtt_payload, "literal", sizeof(source.actions[1].payload.mqtt.mqtt_payload));
    state().action_count = 3;
    begin(43);
    release();
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_EQ(g_action_count, 3u);
    EXPECT_STREQ(g_action_values[0], "43");
    EXPECT_STREQ(g_action_values[1], "literal");
    EXPECT_STREQ(g_action_values[2], "43");
    EXPECT_STREQ(source.actions[0].payload.mqtt.mqtt_payload, "{value}");
}

TEST_F(SliderNativeWidget, LengthIsIndependentOfLabelAndTrackUsesConfiguredThickness) {
    createLayout(0);
    const int width = lv_obj_get_width(state().control);
    lv_label_set_text(state().center_label, "A much longer label");
    slider_layout(&state());
    EXPECT_EQ(lv_obj_get_width(state().control), width);
    auto* settings = reinterpret_cast<SliderConfig*>(config.data);
    settings->track_px = 4;
    settings->knob_px = 20;
    slider_layout(&state());
    EXPECT_EQ(lv_obj_get_height(state().control), 4);
    EXPECT_EQ(lv_obj_get_style_pad_left(state().control, LV_PART_KNOB), 8);
    settings->length_pct = 75;
    slider_layout(&state());
    EXPECT_EQ(lv_obj_get_width(state().control) + 20, lv_obj_get_content_width(button) * 75 / 100);
}

TEST_F(SliderNativeWidget, LargeTransformedIconFitsAndResizesWithoutAccumulatedScaling) {
    createLayout(0);
    state().icon_scale_x = 1024;
    state().icon_scale_y = 1024;
    slider_layout(&state());
    EXPECT_FALSE(lv_obj_has_flag(state().icon, LV_OBJ_FLAG_HIDDEN));
    const int initial_scale = lv_image_get_scale_x(state().icon);
    slider_layout(&state());
    EXPECT_EQ(lv_image_get_scale_x(state().icon), initial_scale);
    lv_obj_set_size(button, 300, 220);
    slider_layout(&state());
    EXPECT_GT(lv_image_get_scale_x(state().icon), initial_scale);
}

TEST_F(SliderNativeWidget, PreprocessedPermissionGuardPreventsNativePress) {
    slider_attach(&runtime, source.actions, source.action_count, [](lv_event_t* event) {
        if (lv_event_get_code(event) == LV_EVENT_PRESSED) lv_event_stop_processing(event);
    }, nullptr);
    lv_area_t area{};
    lv_obj_get_coords(state().control, &area);
    readPointer({area.x2, (area.y1 + area.y2) / 2}, true);
    readPointer({area.x1, (area.y1 + area.y2) / 2}, false);
    EXPECT_FALSE(state().gesture_active);
    EXPECT_EQ(g_dispatches, 0u);
    EXPECT_EQ(lv_slider_get_value(state().control), 0);
}

TEST_F(SliderNativeWidget, ActionMayDestroyOwningButtonSynchronously) {
    g_delete_on_dispatch = button;
    begin(30);
    release();
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_EQ(g_delete_on_dispatch, nullptr);
}

TEST_F(SliderNativeWidget, UnchangedReleaseAndPressLossDoNotDispatch) {
    ASSERT_TRUE(widget_owns_input(g_registered_slider));
    EXPECT_FALSE(widget_owns_input(nullptr));
    begin(40);
    widget_cancel_input(g_registered_slider, &runtime);
    release();
    EXPECT_EQ(g_dispatches, 0u);
    EXPECT_EQ(state().display_position, 0);
    begin(0);
    release();
    EXPECT_EQ(g_dispatches, 0u);
    begin(65);
    lv_obj_send_event(state().control, LV_EVENT_PRESS_LOST, nullptr);
    EXPECT_EQ(g_dispatches, 0u);
    EXPECT_EQ(feedback_count, 0u);
    EXPECT_EQ(state().display_position, 0);
}

TEST_F(SliderNativeWidget, BoundReportsDoNotDispatchAndPendingExpires) {
    json["widget_slider_min"] = 0;
    json["widget_slider_max"] = 1;
    json["widget_slider_step"] = 0.01;
    strlcpy(config.data_binding[0], "[mock:value]", sizeof(config.data_binding[0]));
    slider_destroy(&runtime);
    lv_obj_clean(button);
    runtime = WidgetState{};
    auto* slider_config = reinterpret_cast<SliderConfig*>(config.data);
    ASSERT_TRUE(slider_numeric_range_init(0, 1, 0.01, &slider_config->range));
    slider_create(button, &config, &source, nullptr, &UI_SCALE_TABLE[0],
                  nullptr, nullptr, &runtime);
    g_binding_value = "0";
    slider_tick(button, &config, &runtime);
    EXPECT_TRUE(state().available);
    EXPECT_EQ(state().display_position, 0);
    EXPECT_EQ(g_dispatches, 0u);
    EXPECT_EQ(feedback_count, 0u);

    begin(25);
    EXPECT_TRUE(state().gesture_active);
    EXPECT_EQ(lv_slider_get_value(state().control), 25);
    g_binding_value = "0.2";
    slider_tick(button, &config, &runtime);
    EXPECT_TRUE(state().gesture_active);
    EXPECT_EQ(lv_slider_get_value(state().control), 25);
    EXPECT_EQ(state().draft_position, 25);
    EXPECT_EQ(state().display_position, 25);
    EXPECT_EQ(state().gesture_start_position, 0);
    release();
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_STREQ(g_dispatched_value, "0.25");
    EXPECT_TRUE(state().pending);

    begin(40);
    g_binding_value = "0.3";
    slider_tick(button, &config, &runtime);
    EXPECT_EQ(state().display_position, 40);
    release();
    EXPECT_EQ(g_dispatches, 2u);
    EXPECT_STREQ(g_dispatched_value, "0.4");
    EXPECT_EQ(state().pending_position, 40);

    begin(60);
    lv_obj_send_event(state().control, LV_EVENT_PRESS_LOST, nullptr);
    EXPECT_EQ(state().display_position, 40);
    EXPECT_TRUE(state().pending);

    lv_tick_inc(5000);
    slider_tick(button, &config, &runtime);
    EXPECT_FALSE(state().pending);
    EXPECT_EQ(state().display_position, 30);
}

TEST(SliderSchema, RejectsInvalidRangesActionsAndUnsupportedFields) {
    JsonDocument document;
    JsonObject button = document.to<JsonObject>();
    EXPECT_EQ(slider_validate(button), nullptr);
    button["actions"].to<JsonArray>().add<JsonObject>()["type"] = "mqtt";
    EXPECT_EQ(slider_validate(button), nullptr);

    button["widget_slider_min"] = 0;
    button["widget_slider_max"] = 1;
    button["widget_slider_step"] = 0.3;
    EXPECT_NE(slider_validate(button), nullptr);
    button["widget_slider_step"] = 0.01;
    EXPECT_EQ(slider_validate(button), nullptr);

    button["lp_actions"].to<JsonArray>().add<JsonObject>()["type"] = "mqtt";
    EXPECT_NE(slider_validate(button), nullptr);
    button.remove("lp_actions");
    button["widget_slider_knob_px"] = 4;
    EXPECT_NE(slider_validate(button), nullptr);
    button.remove("widget_slider_knob_px");
    button["widget_slider_ticks"] = true;
    EXPECT_NE(slider_validate(button), nullptr);
}

TEST(SliderNumeric, DefaultAndNegativeRanges) {
    SliderNumericRange range{};
    ASSERT_TRUE(slider_numeric_range_init(0, 100, 1, &range));
    EXPECT_EQ(range.intervals, 100);
    EXPECT_EQ(slider_numeric_value_at(range, range.intervals), 100 * SLIDER_FIXED_SCALE);

    ASSERT_TRUE(slider_numeric_range_init(-10, 10, 0.5, &range));
    EXPECT_EQ(range.intervals, 40);
    EXPECT_EQ(slider_numeric_value_at(range, 0), -10 * SLIDER_FIXED_SCALE);
}

TEST(SliderNumeric, DecimalStepSurvivesBinaryFloatingPoint) {
    SliderNumericRange range{};
    ASSERT_TRUE(slider_numeric_range_init(0, 1, 0.01, &range));
    EXPECT_EQ(range.intervals, 100);
    EXPECT_EQ(slider_numeric_value_at(range, 1), 100);
    EXPECT_EQ(slider_numeric_value_at(range, 100), SLIDER_FIXED_SCALE);
}

TEST(SliderNumeric, ClampsEndpointsAndRoundsTiesTowardLargerValue) {
    SliderNumericRange range{};
    ASSERT_TRUE(slider_numeric_range_init(-1, 1, 0.5, &range));
    EXPECT_EQ(slider_numeric_position_for(range, -2 * SLIDER_FIXED_SCALE), 0);
    EXPECT_EQ(slider_numeric_position_for(range, 2 * SLIDER_FIXED_SCALE), range.intervals);
    EXPECT_EQ(slider_numeric_position_for(range, -2500), 2);
    EXPECT_EQ(slider_numeric_position_for(range, 2500), 3);
}

TEST(SliderNumeric, ParsesSourcesStrictlyAndClampsValidValues) {
    SliderNumericRange range{};
    ASSERT_TRUE(slider_numeric_range_init(-1, 1, 0.01, &range));
    int64_t value = -1;
    ASSERT_TRUE(slider_numeric_parse_source("0", range, &value));
    EXPECT_EQ(value, 0);
    ASSERT_TRUE(slider_numeric_parse_source("1.25", range, &value));
    EXPECT_EQ(value, SLIDER_FIXED_SCALE);
    ASSERT_TRUE(slider_numeric_parse_source("-2", range, &value));
    EXPECT_EQ(value, -SLIDER_FIXED_SCALE);
    EXPECT_FALSE(slider_numeric_parse_source("", range, &value));
    EXPECT_FALSE(slider_numeric_parse_source("0.2tail", range, &value));
    EXPECT_FALSE(slider_numeric_parse_source("0x10", range, &value));
    EXPECT_FALSE(slider_numeric_parse_source("nan", range, &value));
    EXPECT_FALSE(slider_numeric_parse_source("inf", range, &value));
    EXPECT_FALSE(slider_numeric_parse_source("0.00001", range, &value));
}

TEST(SliderNumeric, RejectsUnsupportedPrecisionAndInvalidRanges) {
    SliderNumericRange range{};
    EXPECT_FALSE(slider_numeric_range_init(0, 1, 0.00001, &range));
    EXPECT_FALSE(slider_numeric_range_init(0, 1, 0.3, &range));
    EXPECT_FALSE(slider_numeric_range_init(1, 1, 1, &range));
    EXPECT_FALSE(slider_numeric_range_init(0, 10001, 1, &range));
    EXPECT_FALSE(slider_numeric_range_init(1000000.0001, 1000001, 1, &range));
    EXPECT_TRUE(slider_numeric_range_init(-1000000, 1000000, 2000000, &range));
    EXPECT_EQ(range.intervals, 1);
}

TEST(SliderNumeric, FormatsWithoutExponentOrTrailingZeros) {
    char output[24];
    ASSERT_TRUE(slider_numeric_format(0, output, sizeof(output)));
    EXPECT_STREQ(output, "0");
    ASSERT_TRUE(slider_numeric_format(-123450, output, sizeof(output)));
    EXPECT_STREQ(output, "-12.345");
    ASSERT_TRUE(slider_numeric_format(10000, output, sizeof(output)));
    EXPECT_STREQ(output, "1");
    ASSERT_TRUE(slider_numeric_format(1, output, sizeof(output)));
    EXPECT_STREQ(output, "0.0001");
    std::strcpy(output, "unchanged");
    EXPECT_FALSE(slider_numeric_format(12345, output, 5));
    EXPECT_STREQ(output, "unchanged");
}