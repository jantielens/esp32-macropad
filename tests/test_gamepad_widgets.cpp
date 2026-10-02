#include <gtest/gtest.h>
#include <vector>
#include "board_config.h"
#include "log_manager.h"
#undef HAS_USB_HID
#undef HAS_MQTT
#undef IS_SHUTTER_TESTER
#define HAS_USB_HID 1
#define HAS_MQTT 0
#define IS_SHUTTER_TESTER 0
#define HAS_TOUCH 1
#define HAS_MCP 0
#include "gamepad_hid.h"

namespace {
GamepadHidState widget_controller;
uint32_t widget_generation = 1;
bool widget_ready = true;
}

bool gamepad_hid_is_ready() { return widget_ready; }
uint32_t gamepad_hid_generation() { return widget_generation; }
uint32_t gamepad_hid_acquire(GamepadControl control, uint32_t generation) {
    return widget_ready && generation == widget_generation ? widget_controller.acquire(control) : 0;
}
uint32_t gamepad_hid_acquire_stick(uint8_t stick, uint32_t generation) {
    return widget_ready && generation == widget_generation ? widget_controller.acquire_stick(stick) : 0;
}
bool gamepad_hid_move(uint32_t owner, int16_t horizontal, int16_t vertical, uint32_t generation) {
    return widget_ready && generation == widget_generation && widget_controller.move_stick(owner, horizontal, vertical);
}
void gamepad_hid_release(uint32_t owner, uint32_t generation) {
    if (generation == widget_generation) widget_controller.release(owner);
}

#include "widgets/widget.cpp"
#include "widgets/gamepad_button_widget.cpp"
static unsigned joystick_size_updates = 0;
static unsigned joystick_position_updates = 0;
static void joystick_set_size(lv_obj_t* object, int32_t width, int32_t height) {
    ++joystick_size_updates;
    lv_obj_set_size(object, width, height);
}
static void joystick_set_pos(lv_obj_t* object, int32_t horizontal, int32_t vertical) {
    ++joystick_position_updates;
    lv_obj_set_pos(object, horizontal, vertical);
}
#define lv_obj_set_size joystick_set_size
#define lv_obj_set_pos joystick_set_pos
#include "widgets/gamepad_joystick_widget.cpp"
#undef lv_obj_set_size
#undef lv_obj_set_pos

class GamepadWidget : public testing::Test {
protected:
    lv_display_t* display = nullptr;
    lv_indev_t* indev = nullptr;
    lv_obj_t* button = nullptr;
    const WidgetType* type = nullptr;
    WidgetConfig config{};
    WidgetState state{};
    ScreenButtonConfig button_config{};
    lv_point_t point{50, 50};
    bool pressed = false;
    unsigned ordinary_actions = 0;
    unsigned swipe_actions = 0;
    std::vector<lv_event_code_t> press_feedback;
    uint8_t buffer[300 * 20 * 4]{};

    void SetUp() override {
        static bool initialized = false;
        if (!initialized) { lv_init(); initialized = true; }
        widget_controller.reset();
        ++widget_generation;
        widget_ready = true;
        joystick_size_updates = joystick_position_updates = 0;
        display = lv_display_create(300, 300);
        lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(display, [](lv_display_t* output, const lv_area_t*, uint8_t*) { lv_display_flush_ready(output); });
        indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_user_data(indev, this);
        lv_indev_set_read_cb(indev, [](lv_indev_t* input, lv_indev_data_t* data) {
            auto* fixture = static_cast<GamepadWidget*>(lv_indev_get_user_data(input));
            data->state = fixture->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
            data->point = fixture->point;
        });
        button = lv_obj_create(lv_screen_active());
        lv_obj_set_pos(button, 0, 0);
        lv_obj_set_size(button, 200, 200);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(button, [](lv_event_t* event) {
            auto* fixture = static_cast<GamepadWidget*>(lv_event_get_user_data(event));
            const auto* feedback = static_cast<const lv_event_code_t*>(lv_event_get_param(event));
            ASSERT_NE(feedback, nullptr);
            fixture->press_feedback.push_back(*feedback);
            EXPECT_EQ(lv_obj_has_state(fixture->button, LV_STATE_PRESSED), *feedback == LV_EVENT_PRESSED);
        }, LV_EVENT_VALUE_CHANGED, this);
        button_config.action_count = 1;
        strcpy(button_config.actions[0].type, ACTION_TYPE_GAMEPAD);
        button_config.actions[0].payload.gamepad = {0, 0, 1};
    }

    void create(const char* name) {
        strlcpy(config.type, name, sizeof(config.type));
        ASSERT_STREQ(config.type, name);
        type = widget_find(config.type);
        ASSERT_NE(type, nullptr);
        JsonDocument document;
        type->parseConfig(document.to<JsonObject>(), config.data);
        type->createUI(button, &config, &button_config, nullptr, nullptr, nullptr, nullptr, &state);
        lv_obj_update_layout(button);
        lv_obj_add_event_cb(button, [](lv_event_t* event) {
            ++static_cast<GamepadWidget*>(lv_event_get_user_data(event))->ordinary_actions;
        }, LV_EVENT_SHORT_CLICKED, this);
        lv_obj_add_event_cb(button, [](lv_event_t* event) {
            ++static_cast<GamepadWidget*>(lv_event_get_user_data(event))->ordinary_actions;
        }, LV_EVENT_LONG_PRESSED, this);
        lv_obj_add_event_cb(lv_screen_active(), [](lv_event_t* event) {
            ++static_cast<GamepadWidget*>(lv_event_get_user_data(event))->swipe_actions;
        }, LV_EVENT_GESTURE, this);
    }

    void read(bool down) {
        pressed = down;
        lv_tick_inc(10);
        lv_indev_read(indev);
    }

    GamepadReport delivered() {
        GamepadReport report, last;
        while (widget_controller.next(report)) {
            last = report;
            widget_controller.acknowledge(report, lv_tick_get());
        }
        return last;
    }

    void TearDown() override {
        if (type) type->destroyUI(&state);
        lv_indev_delete(indev);
        lv_display_delete(display);
    }
};

TEST_F(GamepadWidget, HeldButtonConsumesActionsAndReleases) {
    create("gamepad_button");
    read(true);
    ASSERT_TRUE(reinterpret_cast<GamepadButtonState*>(state.data)->configured);
    ASSERT_NE(reinterpret_cast<GamepadButtonState*>(state.data)->touch.owner, 0U);
    EXPECT_EQ(delivered().buttons, 1);
    lv_obj_send_event(button, LV_EVENT_LONG_PRESSED, indev);
    read(false);
    EXPECT_EQ(delivered().buttons, 0);
    EXPECT_EQ(ordinary_actions, 0U);
}

TEST_F(GamepadWidget, HeldButtonRequestsPressReleaseAndCancelFeedback) {
    create("gamepad_button");
    read(true);
    ASSERT_EQ(press_feedback.size(), 1U);
    EXPECT_EQ(press_feedback.back(), LV_EVENT_PRESSED);
    lv_tick_inc(200);
    read(true);
    EXPECT_EQ(press_feedback.size(), 1U);
    read(false);
    ASSERT_EQ(press_feedback.size(), 2U);
    EXPECT_EQ(press_feedback.back(), LV_EVENT_RELEASED);
    read(true);
    type->onHide(&state);
    ASSERT_EQ(press_feedback.size(), 4U);
    EXPECT_EQ(press_feedback.back(), LV_EVENT_PRESS_LOST);
    EXPECT_EQ(ordinary_actions, 0U);
}

TEST_F(GamepadWidget, RejectedButtonDoesNotRequestPressFeedback) {
    create("gamepad_button");
    widget_ready = false;
    read(true);
    read(false);
    EXPECT_TRUE(press_feedback.empty());
    EXPECT_FALSE(lv_obj_has_state(button, LV_STATE_PRESSED));
    EXPECT_EQ(ordinary_actions, 0U);
}

TEST_F(GamepadWidget, IndevResetReleasesOnlyWidgetOwnership) {
    create("gamepad_button");
    read(true);
    delivered();
    ASSERT_TRUE(widget_controller.hold({GamepadControlKind::Button, 0}, true));
    lv_indev_reset(indev, nullptr);
    EXPECT_EQ(reinterpret_cast<GamepadButtonState*>(state.data)->touch.owner, 0U);
    ASSERT_TRUE(widget_controller.hold({GamepadControlKind::Button, 0}, false));
    EXPECT_EQ(delivered().buttons, 0);
    EXPECT_EQ(ordinary_actions, 0U);
}

TEST_F(GamepadWidget, HideAndDestroyCancelHolds) {
    create("gamepad_button");
    read(true);
    delivered();
    type->onHide(&state);
    EXPECT_EQ(delivered().buttons, 0);
    read(false);
    read(true);
    EXPECT_EQ(delivered().buttons, 1);
    type->destroyUI(&state);
    type = nullptr;
    EXPECT_EQ(delivered().buttons, 0);
}

TEST_F(GamepadWidget, JoystickKeepsStationaryDeflectionAndCancels) {
    create("gamepad_stick");
    point = {175, 100};
    read(true);
    EXPECT_EQ(delivered().left_x, 32767);
    read(true);
    GamepadReport report;
    EXPECT_FALSE(widget_controller.next(report));
    EXPECT_GT(lv_obj_get_width(reinterpret_cast<GamepadJoystickState*>(state.data)->base), 0);
    read(false);
    EXPECT_EQ(delivered().left_x, 0);
    EXPECT_EQ(ordinary_actions, 0U);
}

TEST_F(GamepadWidget, JoystickCapturesCenteredDragWithoutDeviceSwipe) {
    create("gamepad_stick");
    point = {100, 100};
    read(true);
    ASSERT_NE(reinterpret_cast<GamepadJoystickState*>(state.data)->touch.owner, 0U);
    EXPECT_EQ(delivered().left_x, 0);
    point = {175, 100};
    read(true);
    EXPECT_EQ(delivered().left_x, 32767);
    EXPECT_EQ(swipe_actions, 0U);
    read(false);
    EXPECT_EQ(delivered().left_x, 0);
    EXPECT_EQ(ordinary_actions, 0U);
}

TEST_F(GamepadWidget, JoystickMovementOnlyUpdatesChangedThumbPosition) {
    create("gamepad_stick");
    const unsigned sizes = joystick_size_updates;
    const unsigned positions = joystick_position_updates;
    point = {100, 100};
    read(true);
    EXPECT_EQ(joystick_size_updates, sizes);
    EXPECT_EQ(joystick_position_updates, positions);
    point = {175, 100};
    read(true);
    EXPECT_EQ(joystick_size_updates, sizes);
    EXPECT_EQ(joystick_position_updates, positions + 1);
    read(true);
    EXPECT_EQ(joystick_position_updates, positions + 1);
    read(false);
    EXPECT_EQ(joystick_size_updates, sizes);
    EXPECT_EQ(joystick_position_updates, positions + 2);
}

TEST_F(GamepadWidget, JoystickUsesForegroundAndFillsPaddedContent) {
    lv_obj_set_size(button, 240, 180);
    lv_obj_set_style_pad_all(button, 12, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, lv_color_hex(0xe06040), LV_PART_MAIN);
    create("gamepad_stick");
    auto* joystick = reinterpret_cast<GamepadJoystickState*>(state.data);
    EXPECT_EQ(lv_obj_get_width(joystick->base), 156);
    EXPECT_EQ(lv_obj_get_height(joystick->base), 156);
    EXPECT_EQ(lv_obj_get_width(joystick->thumb), 52);
    EXPECT_TRUE(lv_color_eq(lv_obj_get_style_border_color(joystick->base, LV_PART_MAIN), lv_color_hex(0xe06040)));
    EXPECT_TRUE(lv_color_eq(lv_obj_get_style_bg_color(joystick->thumb, LV_PART_MAIN), lv_color_hex(0xe06040)));

    point = {220, 90};
    read(true);
    EXPECT_EQ(delivered().left_x, 32767);
    lv_obj_update_layout(button);
    lv_area_t content, base, thumb;
    lv_obj_get_content_coords(button, &content);
    lv_obj_get_coords(joystick->base, &base);
    lv_obj_get_coords(joystick->thumb, &thumb);
    EXPECT_GE(base.x1, content.x1);
    EXPECT_GE(base.y1, content.y1);
    EXPECT_LE(base.x2, content.x2);
    EXPECT_LE(base.y2, content.y2);
    EXPECT_GE(thumb.x1, base.x1);
    EXPECT_GE(thumb.y1, base.y1);
    EXPECT_LE(thumb.x2, base.x2);
    EXPECT_LE(thumb.y2, base.y2);

    lv_obj_set_style_text_color(button, lv_color_hex(0x60a0e0), LV_PART_MAIN);
    gamepad_stick_tick(button, &config, &state);
    EXPECT_TRUE(lv_color_eq(lv_obj_get_style_border_color(joystick->base, LV_PART_MAIN), lv_color_hex(0x60a0e0)));
    EXPECT_TRUE(lv_color_eq(lv_obj_get_style_bg_color(joystick->thumb, LV_PART_MAIN), lv_color_hex(0x60a0e0)));
}

TEST_F(GamepadWidget, ValidatorsWorkWithoutMcp) {
    create("gamepad_button");
    JsonDocument document;
    JsonObject button_json = document.to<JsonObject>();
    EXPECT_NE(type->validateConfig(button_json), nullptr);
    JsonObject action = button_json["actions"].to<JsonArray>().add<JsonObject>();
    action["type"] = "gamepad";
    action["operation"] = "down";
    EXPECT_EQ(type->validateConfig(button_json), nullptr);
    action["operation"] = "tap";
    EXPECT_NE(type->validateConfig(button_json), nullptr);
    button_json["widget_gamepad_dead_zone"] = 1;
    EXPECT_NE(gamepad_stick_validate(button_json), nullptr);
}

TEST(GamepadWidgetRegistry, SupportsFullFeaturedBoards) {
    static WidgetType placeholders[14] = {};
    for (auto& placeholder : placeholders) {
        placeholder.name = "capacity_probe";
        widget_register(&placeholder);
    }
    EXPECT_EQ(widget_count(), 16);
    EXPECT_EQ(widget_at(15), &placeholders[13]);
}