#include <gtest/gtest.h>
#include <functional>
#include "widgets/toggle_widget.cpp"
#include "expr_binding.h"

void widget_register(const WidgetType*) {}
static unsigned dispatches;
static std::string dispatched;
static ActionResult dispatch_result;
static std::function<void()> during_dispatch;
static const char* thermostat_value = "10";

ActionResult action_list_dispatch_result(const ButtonAction* actions, uint8_t count, const char*, ActionContinuationOwner) {
    if (count) { ++dispatches; dispatched = actions[0].payload.ha_service.data_json; }
    if (during_dispatch) during_dispatch();
    return dispatch_result;
}

class ToggleWidget : public testing::Test {
protected:
    lv_display_t* display;
    lv_obj_t* button;
    WidgetConfig config{};
    WidgetState runtime{};
    ScreenButtonConfig source{};
    ButtonAction on_action{}, off_action{};
    JsonDocument json;

    void SetUp() override {
        static bool initialized = false;
        if (!initialized) { lv_init(); initialized = true; }
        display = lv_display_create(320, 240);
        button = lv_obj_create(lv_screen_active());
        lv_obj_set_size(button, 200, 180);
        dispatches = 0;
        dispatched.clear();
        dispatch_result = ACTION_COMPLETE;
        during_dispatch = {};
        strlcpy(on_action.payload.ha_service.data_json, "{\"temperature\": 21}", sizeof(on_action.payload.ha_service.data_json));
        strlcpy(off_action.payload.ha_service.data_json, "{\"temperature\": 10}", sizeof(off_action.payload.ha_service.data_json));
    }
    void TearDown() override { during_dispatch = {}; lv_display_delete(display); }
    ToggleState& state() { return *reinterpret_cast<ToggleState*>(runtime.data); }
    void create(const char* binding = "") {
        toggle_parse(json.to<JsonObject>(), config.data);
        strlcpy(config.data_binding[0], binding, sizeof(config.data_binding[0]));
        toggle_create(button, &config, &source, nullptr, nullptr, nullptr, nullptr, &runtime);
    }
    void tap(uint8_t count = 1) { toggle_tap(&config, &runtime, &on_action, count, &off_action, count); }
    static void click(lv_event_t* event) {
        static_cast<ToggleWidget*>(lv_event_get_user_data(event))->tap();
    }
    void report(const char* value, bool valid = true) { toggle_report(&config, &runtime, value, valid); }
};

TEST_F(ToggleWidget, LocalAlternatesActionsAndResetsOnHide) {
    create();
    EXPECT_FALSE(state().value);
    EXPECT_EQ(dispatches, 0u);
    tap(); EXPECT_TRUE(state().value); EXPECT_EQ(dispatched, "{\"temperature\": 21}");
    tap(); EXPECT_FALSE(state().value); EXPECT_EQ(dispatched, "{\"temperature\": 10}");
    dispatch_result = ACTION_FAILED;
    tap(); EXPECT_TRUE(state().value);
    EXPECT_EQ(dispatches, 3u);
    toggle_hide(&runtime); EXPECT_FALSE(state().value);
    tap(0); EXPECT_TRUE(state().value); EXPECT_EQ(dispatches, 3u);
}

TEST_F(ToggleWidget, BoundReportsConfirmCategoriesNotExactTemperatureCommands) {
    create("[ha:climate.bureau_boven_thermostaat;attributes.temperature]");
    EXPECT_FALSE(state().known); EXPECT_FALSE(toggle_interactive(&runtime));
    tap(); EXPECT_EQ(dispatches, 0u);
    report(" OFF \t"); EXPECT_TRUE(toggle_interactive(&runtime));
    during_dispatch = [&] { EXPECT_TRUE(state().pending); };
    tap(); EXPECT_TRUE(state().pending); EXPECT_FALSE(state().value);
    tap(); EXPECT_EQ(dispatches, 1u);
    report("off"); EXPECT_TRUE(state().pending);
    report("on"); EXPECT_FALSE(state().pending); EXPECT_TRUE(state().value);
    tap(); EXPECT_EQ(dispatched, "{\"temperature\": 10}");
    report("off"); EXPECT_FALSE(state().pending); EXPECT_FALSE(state().value);
    EXPECT_EQ(dispatches, 2u);
}

TEST_F(ToggleWidget, UnchangedReportsDoNotInvalidateButVisualTransitionsDo) {
    create("off");
    toggle_tick(button, &config, &runtime);
    lv_obj_update_layout(button);
    unsigned invalidations = 0;
    const auto observe = [](lv_event_t* event) {
        ++*static_cast<unsigned*>(lv_event_get_user_data(event));
    };
    lv_display_add_event_cb(display, observe, LV_EVENT_INVALIDATE_AREA, &invalidations);
    toggle_tick(button, &config, &runtime);
    report(" OFF ");
    EXPECT_EQ(invalidations, 0u);
    tap();
    EXPECT_GT(invalidations, 0u);
    invalidations = 0;
    report("off");
    EXPECT_EQ(invalidations, 0u);
    EXPECT_TRUE(state().pending);
    report("on");
    EXPECT_GT(invalidations, 0u);
    EXPECT_FALSE(state().pending);
    invalidations = 0;
    report("on");
    EXPECT_EQ(invalidations, 0u);
    report("unknown");
    EXPECT_GT(invalidations, 0u);
    invalidations = 0;
    report("unavailable");
    EXPECT_EQ(invalidations, 0u);
    report("on");
    EXPECT_GT(invalidations, 0u);
    lv_display_remove_event_cb_with_user_data(display, observe, &invalidations);
}

TEST_F(ToggleWidget, UnavailabilityRecoveryReadonlyAndEmptyActions) {
    create("on");
    toggle_tick(button, &config, &runtime);
    EXPECT_TRUE(state().value);
    tap(0); EXPECT_FALSE(state().pending);
    tap(); EXPECT_TRUE(state().pending);
    report("off", false); EXPECT_FALSE(state().available); EXPECT_TRUE(state().value); EXPECT_FALSE(state().pending);
    tap(); EXPECT_EQ(dispatches, 1u);
    report("unknown"); EXPECT_FALSE(state().available);
    report("off"); EXPECT_TRUE(state().available); EXPECT_FALSE(state().value);
    state().read_only = true;
    tap(); EXPECT_FALSE(state().value); EXPECT_EQ(dispatches, 1u);
    report("on"); EXPECT_TRUE(state().value); EXPECT_TRUE(state().available);
}

TEST_F(ToggleWidget, TimeoutWrapFailureFastConfirmationAndNavigationDeletion) {
    create("off");
    report("off");
    lv_tick_inc(UINT32_MAX - lv_tick_get() - 1000);
    tap();
    lv_tick_inc(4999); toggle_tick(button, &config, &runtime); EXPECT_TRUE(state().pending);
    lv_tick_inc(1); toggle_tick(button, &config, &runtime); EXPECT_FALSE(state().pending);
    dispatch_result = ACTION_FAILED;
    tap(); EXPECT_FALSE(state().pending);
    dispatch_result = ACTION_COMPLETE;
    during_dispatch = [&] { report("on"); };
    tap(); EXPECT_FALSE(state().pending); EXPECT_TRUE(state().value);
    during_dispatch = [&] { lv_obj_delete(button); button = nullptr; };
    tap(); EXPECT_EQ(button, nullptr);
}

TEST_F(ToggleWidget, ConfigDefaultsValidationAndBoundedFitting) {
    JsonObject settings = json.to<JsonObject>();
    EXPECT_EQ(toggle_validate(settings), nullptr);
    settings["widget_toggle_on_value"] = " OFF ";
    EXPECT_NE(toggle_validate(settings), nullptr);
    settings["widget_toggle_on_value"] = std::string(64, 'x');
    EXPECT_NE(toggle_validate(settings), nullptr);
    settings["widget_toggle_on_value"] = "yes";
    settings["widget_toggle_width_pct"] = 101;
    EXPECT_NE(toggle_validate(settings), nullptr);
    settings["widget_toggle_width_pct"] = 60;
    settings["widget_toggle_on_color"] = "[health:cpu]";
    EXPECT_NE(toggle_validate(settings), nullptr);
    settings.remove("widget_toggle_on_color");
    EXPECT_EQ(toggle_validate(settings), nullptr);
    toggle_parse(settings, config.data);
    EXPECT_EQ(reinterpret_cast<ToggleConfig*>(config.data)->width_pct, 60);
    EXPECT_STREQ(reinterpret_cast<ToggleConfig*>(config.data)->on_value, "yes");
    lv_obj_set_size(button, 20, 20);
    toggle_create(button, &config, &source, nullptr, nullptr, nullptr, nullptr, &runtime);
    lv_obj_update_layout(button);
    EXPECT_EQ(lv_obj_get_width(state().control), 2 * lv_obj_get_height(state().control));
    EXPECT_FALSE(lv_obj_has_flag(state().control, LV_OBJ_FLAG_CLICKABLE));
}

TEST_F(ToggleWidget, LabelsAndExistingIconReserveSwitchSpace) {
    toggle_parse(json.to<JsonObject>(), config.data);
    lv_obj_t* top = lv_label_create(button);
    lv_label_set_text(top, "Bureau");
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t* bottom = lv_label_create(button);
    lv_label_set_text(bottom, "21.0 C");
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t* icon = lv_obj_create(button);
    lv_obj_set_size(icon, 32, 32);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);
    toggle_create(button, &config, &source, nullptr, nullptr, icon, nullptr, &runtime);
    lv_obj_update_layout(button);
    lv_area_t top_bounds, bottom_bounds, icon_bounds, switch_bounds;
    lv_obj_get_coords(top, &top_bounds);
    lv_obj_get_coords(bottom, &bottom_bounds);
    lv_obj_get_coords(icon, &icon_bounds);
    lv_obj_get_coords(state().control, &switch_bounds);
    EXPECT_LT(top_bounds.y2, icon_bounds.y1);
    EXPECT_LT(icon_bounds.x2, switch_bounds.x1);
    EXPECT_NEAR((icon_bounds.y1 + icon_bounds.y2) / 2, (switch_bounds.y1 + switch_bounds.y2) / 2, 1);
    EXPECT_LT(switch_bounds.y2, bottom_bounds.y1);
}

TEST_F(ToggleWidget, AvailableSpaceFitsBetweenOuterLabelsAndScalesFromMaximum) {
    for (unsigned labels = 0; labels < 4; ++labels) {
        lv_obj_clean(button);
        lv_obj_t* top = nullptr;
        lv_obj_t* bottom = nullptr;
        if (labels & 1) {
            top = lv_label_create(button);
            lv_label_set_text(top, "Bureau");
            lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
        }
        if (labels & 2) {
            bottom = lv_label_create(button);
            lv_label_set_text(bottom, "21.0 C");
            lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
        }
        create();
        lv_obj_update_layout(button);
        const int top_edge = top ? lv_obj_get_y(top) + lv_obj_get_height(top) + 4 : 0;
        const int bottom_edge = bottom ? lv_obj_get_y(bottom) - 4 : lv_obj_get_content_height(button);
        int maximum = std::min(lv_obj_get_content_width(button), (bottom_edge - top_edge) * 2);
        maximum -= maximum % 2;
        EXPECT_EQ(lv_obj_get_width(state().control), maximum);
        EXPECT_EQ(lv_obj_get_height(state().control), maximum / 2);
        EXPECT_GE(lv_obj_get_y(state().control), top_edge);
        EXPECT_LE(lv_obj_get_y(state().control) + maximum / 2, bottom_edge);
        reinterpret_cast<ToggleConfig*>(config.data)->width_pct = 60;
        toggle_tick(button, &config, &runtime);
        lv_obj_update_layout(button);
        int smaller = maximum * 60 / 100;
        smaller -= smaller % 2;
        EXPECT_EQ(lv_obj_get_width(state().control), smaller);
    }
}

TEST_F(ToggleWidget, CenterLabelSharesRowAndBoundTextRelayoutsWithoutActions) {
    toggle_parse(json.to<JsonObject>(), config.data);
    lv_obj_t* center = lv_label_create(button);
    lv_obj_set_width(center, lv_obj_get_content_width(button));
    lv_label_set_text(center, "Heat");
    lv_obj_align(center, LV_ALIGN_CENTER, 0, 0);
    toggle_create(button, &config, &source, nullptr, nullptr, nullptr, center, &runtime);
    lv_obj_update_layout(button);
    EXPECT_LT(lv_obj_get_x(center) + lv_obj_get_width(center), lv_obj_get_x(state().control));
    EXPECT_NEAR(lv_obj_get_y(center) + lv_obj_get_height(center) / 2,
                lv_obj_get_y(state().control) + lv_obj_get_height(state().control) / 2, 1);
    const int first_width = lv_obj_get_width(state().control);
    lv_label_set_text(center, "Comfort heating");
    toggle_tick(button, &config, &runtime);
    lv_obj_update_layout(button);
    EXPECT_LT(lv_obj_get_width(state().control), first_width);
    EXPECT_LT(lv_obj_get_x(center) + lv_obj_get_width(center), lv_obj_get_x(state().control));
    EXPECT_EQ(dispatches, 0u);
}

TEST_F(ToggleWidget, IconPositionsAndGroupOffsetDoNotOverlapCenterLabel) {
    for (const char* position : {"left", "above", "below"}) {
        lv_obj_clean(button);
        JsonObject settings = json.to<JsonObject>();
        settings["widget_toggle_icon_position"] = position;
        EXPECT_EQ(toggle_validate(settings), nullptr);
        toggle_parse(settings, config.data);
        lv_obj_t* center = lv_label_create(button);
        lv_label_set_text(center, "Heat");
        lv_obj_t* icon = lv_obj_create(button);
        lv_obj_set_size(icon, 24, 24);
        toggle_create(button, &config, &source, nullptr, nullptr, icon, center, &runtime);
        lv_obj_update_layout(button);
        if (strcmp(position, "left") == 0) EXPECT_LT(lv_obj_get_x(icon) + 24, lv_obj_get_x(center));
        else if (strcmp(position, "above") == 0) {
            EXPECT_LE(lv_obj_get_y(icon) + 24, lv_obj_get_y(center));
            EXPECT_LE(lv_obj_get_y(icon) + 24, lv_obj_get_y(state().control));
        } else {
            EXPECT_GE(lv_obj_get_y(icon), lv_obj_get_y(center) + lv_obj_get_height(center));
            EXPECT_GE(lv_obj_get_y(icon), lv_obj_get_y(state().control) + lv_obj_get_height(state().control));
        }
        const int control_x = lv_obj_get_x(state().control), control_y = lv_obj_get_y(state().control);
        const int icon_x = lv_obj_get_x(icon), icon_y = lv_obj_get_y(icon);
        state().offset_x = 3; state().offset_y = -2;
        toggle_tick(button, &config, &runtime);
        lv_obj_update_layout(button);
        EXPECT_EQ(lv_obj_get_x(state().control), control_x + 3);
        EXPECT_EQ(lv_obj_get_y(state().control), control_y - 2);
        EXPECT_EQ(lv_obj_get_x(icon), icon_x + 3);
        EXPECT_EQ(lv_obj_get_y(icon), icon_y - 2);
    }
}

TEST_F(ToggleWidget, UpscaledLabelsReserveTheirPaintedBounds) {
    lv_obj_set_size(button, 300, 180);
    toggle_parse(json.to<JsonObject>(), config.data);
    lv_obj_t* top = lv_label_create(button);
    lv_label_set_text(top, "Bureau");
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t* bottom = lv_label_create(button);
    lv_label_set_text(bottom, "21.0 C");
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t* center = lv_label_create(button);
    lv_label_set_text(center, "On");
    LabelStyle style{};
    style.font_upscale = 512;
    pad_apply_font_upscale(top, style, PAD_LABEL_ANCHOR_TOP);
    pad_apply_font_upscale(bottom, style, PAD_LABEL_ANCHOR_BOTTOM);
    pad_apply_font_upscale(center, style, PAD_LABEL_ANCHOR_CENTER);
    toggle_create(button, &config, &source, nullptr, nullptr, nullptr, center, &runtime);
    lv_obj_update_layout(button);
    lv_area_t top_bounds, bottom_bounds, center_bounds, switch_bounds;
    lv_obj_get_coords(top, &top_bounds);
    lv_obj_get_transformed_area(top, &top_bounds, LV_OBJ_POINT_TRANSFORM_FLAG_NONE);
    lv_obj_get_coords(bottom, &bottom_bounds);
    lv_obj_get_transformed_area(bottom, &bottom_bounds, LV_OBJ_POINT_TRANSFORM_FLAG_NONE);
    lv_obj_get_coords(center, &center_bounds);
    lv_obj_get_transformed_area(center, &center_bounds, LV_OBJ_POINT_TRANSFORM_FLAG_NONE);
    lv_obj_get_coords(state().control, &switch_bounds);
    EXPECT_LT(top_bounds.y2, switch_bounds.y1);
    EXPECT_LT(switch_bounds.y2, bottom_bounds.y1);
    EXPECT_LT(center_bounds.x2, switch_bounds.x1);
    EXPECT_NEAR((center_bounds.y1 + center_bounds.y2) / 2, (switch_bounds.y1 + switch_bounds.y2) / 2, 1);
}

TEST_F(ToggleWidget, ScaledImagePivotsFitBesideAboveAndBelowSwitch) {
    static const uint8_t pixels[24 * 24 * 2] = {};
    lv_image_dsc_t image{};
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_RGB565;
    image.header.w = 24;
    image.header.h = 24;
    image.header.stride = 24 * 2;
    image.data_size = sizeof(pixels);
    image.data = pixels;
    for (const char* position : {"left", "above", "below"}) {
        lv_obj_clean(button);
        JsonObject settings = json.to<JsonObject>();
        settings["widget_toggle_icon_position"] = position;
        toggle_parse(settings, config.data);
        lv_obj_t* icon = lv_image_create(button);
        lv_image_set_src(icon, &image);
        lv_image_set_pivot(icon, 4, 6);
        lv_image_set_scale(icon, 512);
        toggle_create(button, &config, &source, nullptr, nullptr, icon, nullptr, &runtime);
        lv_obj_update_layout(button);
        lv_area_t icon_bounds, switch_bounds;
        lv_obj_get_coords(icon, &icon_bounds);
        icon_bounds.x1 -= 4;
        icon_bounds.y1 -= 6;
        icon_bounds.x2 = icon_bounds.x1 + 47;
        icon_bounds.y2 = icon_bounds.y1 + 47;
        lv_obj_get_coords(state().control, &switch_bounds);
        if (strcmp(position, "left") == 0) EXPECT_LT(icon_bounds.x2, switch_bounds.x1);
        else if (strcmp(position, "above") == 0) EXPECT_LT(icon_bounds.y2, switch_bounds.y1);
        else EXPECT_LT(switch_bounds.y2, icon_bounds.y1);
        EXPECT_FALSE(lv_obj_has_flag(state().control, LV_OBJ_FLAG_HIDDEN));
    }
}

TEST_F(ToggleWidget, TrackAndBackgroundShortClicksDispatchOnceButHoldAndCancelDoNot) {
    create();
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(button, click, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_update_layout(button);
    struct Input { lv_point_t point; bool pressed; } input{};
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, display);
    lv_indev_set_user_data(indev, &input);
    lv_indev_set_read_cb(indev, [](lv_indev_t* device, lv_indev_data_t* data) {
        auto* current = static_cast<Input*>(lv_indev_get_user_data(device));
        data->point = current->point;
        data->state = current->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    });
    lv_area_t bounds;
    lv_obj_get_coords(state().control, &bounds);
    input.point = {(bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2};
    input.pressed = true; lv_indev_read(indev);
    input.pressed = false; lv_indev_read(indev);
    EXPECT_EQ(dispatches, 1u); EXPECT_TRUE(state().value);
    lv_obj_get_coords(button, &bounds);
    input.point = {bounds.x1 + 6, bounds.y1 + 6};
    input.pressed = true; lv_indev_read(indev);
    input.pressed = false; lv_indev_read(indev);
    EXPECT_EQ(dispatches, 2u); EXPECT_FALSE(state().value);
    input.pressed = true; lv_indev_read(indev);
    lv_tick_inc(1000); lv_indev_read(indev);
    input.pressed = false; lv_indev_read(indev);
    EXPECT_EQ(dispatches, 2u);
    input.pressed = true; lv_indev_read(indev);
    lv_indev_reset(indev, button);
    input.pressed = false; lv_indev_read(indev);
    EXPECT_EQ(dispatches, 2u);
    lv_indev_delete(indev);
}

TEST_F(ToggleWidget, BureauExpressionReportsConfirmRequestedCategoryAndRejectInvalidInputs) {
    ASSERT_TRUE(binding_template_register("ha", [](const char*, char* out, size_t size) {
        if (!thermostat_value) return BINDING_RESOLVER_UNAVAILABLE;
        snprintf(out, size, "%s", thermostat_value);
        return BINDING_RESOLVER_RESOLVED;
    }, nullptr, {1, 3, 2, 2, BINDING_VALIDATION_STANDARD, true}));
    expr_binding_init();
    create("[expr:[ha:climate.bureau_boven_thermostaat;attributes.temperature]>20?\"on\":\"off\"]");
    thermostat_value = "10";
    toggle_tick(button, &config, &runtime);
    EXPECT_FALSE(state().value); EXPECT_TRUE(state().available);
    tap(); EXPECT_TRUE(state().pending); EXPECT_EQ(dispatched, "{\"temperature\": 21}");
    thermostat_value = "22";
    toggle_tick(button, &config, &runtime);
    EXPECT_TRUE(state().value); EXPECT_FALSE(state().pending);
    EXPECT_EQ(dispatches, 1u);
    tap(); EXPECT_TRUE(state().pending); EXPECT_EQ(dispatched, "{\"temperature\": 10}");
    thermostat_value = "20";
    toggle_tick(button, &config, &runtime);
    EXPECT_FALSE(state().value); EXPECT_FALSE(state().pending);
    for (const char* invalid : {"", "unknown", "unavailable", "oops", "21oops"}) {
        thermostat_value = invalid;
        toggle_tick(button, &config, &runtime);
        EXPECT_FALSE(state().available);
        tap(); EXPECT_EQ(dispatches, 2u);
    }
    thermostat_value = nullptr;
    toggle_tick(button, &config, &runtime);
    EXPECT_FALSE(state().available);
    thermostat_value = "21";
    toggle_tick(button, &config, &runtime);
    EXPECT_TRUE(state().available); EXPECT_TRUE(state().value);
    EXPECT_EQ(dispatches, 2u);
    toggle_hide(&runtime);
    EXPECT_FALSE(state().pending); EXPECT_FALSE(state().available);
    toggle_tick(button, &config, &runtime);
    EXPECT_TRUE(state().value); EXPECT_EQ(dispatches, 2u);
}