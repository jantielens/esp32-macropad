#include <gtest/gtest.h>
#include <vector>
#include <lvgl.h>
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
#undef HAS_AUDIO
#define HAS_AUDIO 0
#include "gamepad_hid.h"
#include "mouse_hid_state.h"

namespace {
GamepadHidState widget_controller;
uint32_t widget_generation = 1;
bool widget_ready = true;
unsigned widget_back_requests = 0;
MouseHidState widget_mouse;
uint32_t widget_mouse_epoch = 1;
}

bool mouse_hid_is_ready() { return widget_ready; }
bool display_manager_go_back() { ++widget_back_requests; return true; }
uint32_t usb_hid_epoch() { return widget_mouse_epoch; }
uint32_t mouse_hid_generation() { return widget_mouse.current_generation(); }
void mouse_hid_cancel() { widget_mouse.reset(); }
uint32_t mouse_hid_acquire(uint32_t, uint8_t buttons) { return widget_ready ? widget_mouse.acquire(buttons) : 0; }
void mouse_hid_release(uint32_t owner, uint32_t) { widget_mouse.release(owner); }
void mouse_hid_begin_touch() { widget_mouse.stop_scroll(); }
void mouse_hid_move(int dx, int dy, uint32_t) { widget_mouse.move(dx, dy); }
void mouse_hid_scroll(int wheel, int pan, uint32_t) { widget_mouse.scroll(wheel, pan); }
bool mouse_hid_click(uint32_t, uint8_t buttons) { return widget_mouse.click(buttons); }
void mouse_hid_start_scroll_inertia(float velocity, float inertia, bool horizontal, uint32_t) {
    widget_mouse.start_inertia(velocity, inertia, horizontal, lv_tick_get());
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
static unsigned touch_metadata_scans = 0;
static uint32_t touch_event_count(lv_obj_t* object) {
    ++touch_metadata_scans;
    return lv_obj_get_event_count(object);
}
#define lv_obj_get_event_count touch_event_count
#include "widgets/gamepad_button_widget.cpp"
#include "widgets/mouse_surface_touch.h"
#undef lv_obj_get_event_count
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
#include "widgets/gamepad_touch_router.h"
#include "widgets/mousepad_widget.cpp"
#include "widgets/scrollpad_widget.cpp"
#include "swipe_actions.cpp"

static unsigned navigation_swipe_requests = 0;
static SwipeConfig navigation_swipe_config{};
const SwipeConfig* swipe_config_get() { return &navigation_swipe_config; }
ActionResult action_dispatch(const ButtonAction&, const char*, uint32_t) {
    ++navigation_swipe_requests;
    return ACTION_COMPLETE;
}

TEST(WidgetNavigation, AnyEnabledInputWidgetSuppressesTheEntirePad) {
    ScreenButtonConfig buttons[2]{};
    PadConfig pad{};
    pad.buttons = buttons;
    pad.button_count = 2;
    strcpy(buttons[0].widget.type, "gauge");
    strcpy(buttons[1].widget.type, "scrollpad");
    EXPECT_FALSE(pad_disables_swipes(pad));
    buttons[0].widget.disable_pad_swipes = true;
    EXPECT_FALSE(pad_disables_swipes(pad));
    buttons[1].widget.disable_pad_swipes = true;
    EXPECT_TRUE(pad_disables_swipes(pad));
    buttons[1].widget.disable_pad_swipes = false;
    EXPECT_FALSE(pad_disables_swipes(pad));
}

TEST(WidgetNavigation, SwipeControlIsLimitedToInputWidgetsAndDefaultsOff) {
    WidgetConfig config{};
    for (const char* name : {"mousepad", "scrollpad", "gamepad_button", "gamepad_stick"}) {
        strlcpy(config.type, name, sizeof(config.type));
        config.disable_pad_swipes = false;
        EXPECT_FALSE(widget_disables_pad_swipes(config));
        config.disable_pad_swipes = true;
        EXPECT_TRUE(widget_disables_pad_swipes(config));
    }
    for (const char* name : {"", "gauge", "clock"}) {
        strlcpy(config.type, name, sizeof(config.type));
        EXPECT_FALSE(widget_disables_pad_swipes(config));
    }
}

class GamepadWidget : public testing::Test {
protected:
    lv_display_t* display = nullptr;
    lv_indev_t* indev = nullptr;
    lv_obj_t* button = nullptr;
    const WidgetType* type = nullptr;
    WidgetConfig config{};
    WidgetState state{};
    ScreenButtonConfig button_config{};
    lv_obj_t* extra_button = nullptr;
    const WidgetType* extra_type = nullptr;
    WidgetConfig extra_config{};
    WidgetState extra_state{};
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
        widget_mouse.reset();
        ++widget_generation;
        widget_ready = true;
        widget_back_requests = 0;
        navigation_swipe_requests = 0;
        for (ButtonAction* action : {&navigation_swipe_config.swipe_left, &navigation_swipe_config.swipe_right,
                                     &navigation_swipe_config.swipe_up, &navigation_swipe_config.swipe_down}) {
            strcpy(action->type, "test");
        }
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

    void create(const char* name, bool mouse_buttons = false, bool show_back = false) {
        strlcpy(config.type, name, sizeof(config.type));
        ASSERT_STREQ(config.type, name);
        type = widget_find(config.type);
        ASSERT_NE(type, nullptr);
        JsonDocument document;
        document["widget_mousepad_buttons"] = mouse_buttons;
        document["widget_mousepad_back"] = show_back;
        type->parseConfig(document.as<JsonObject>(), config.data);
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

    void create_extra(const char* name, GamepadControl control = {GamepadControlKind::Button, 1}) {
        extra_button = lv_obj_create(lv_screen_active());
        lv_obj_set_pos(extra_button, 150, 0);
        lv_obj_set_size(extra_button, 150, 150);
        lv_obj_add_flag(extra_button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(extra_button, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(extra_button, 0, LV_PART_MAIN);
        extra_type = widget_find(name);
        ASSERT_NE(extra_type, nullptr);
        strlcpy(extra_config.type, name, sizeof(extra_config.type));
        JsonDocument document;
        document["widget_gamepad_stick"] = "right";
        extra_type->parseConfig(document.as<JsonObject>(), extra_config.data);
        ScreenButtonConfig configuration = button_config;
        configuration.actions[0].payload.gamepad = {uint8_t(control.kind), control.index, 1};
        extra_type->createUI(extra_button, &extra_config, &configuration, nullptr, nullptr, nullptr, nullptr, &extra_state);
        lv_obj_update_layout(extra_button);
    }

    GamepadReport delivered() {
        GamepadReport report, last;
        while (widget_controller.next(report)) {
            last = report;
            widget_controller.acknowledge(report, lv_tick_get());
        }
        return last;
    }

    std::vector<MouseHidReport> mouse_reports() {
        std::vector<MouseHidReport> reports;
        MouseHidReport report;
        while (widget_mouse.next(report)) {
            reports.push_back(report);
            widget_mouse.acknowledge(report);
        }
        return reports;
    }

    void TearDown() override {
        if (extra_type) extra_type->destroyUI(&extra_state);
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

TEST_F(GamepadWidget, RouterHandlesDeletionInsidePressFeedback) {
    create("gamepad_button");
    lv_obj_add_event_cb(button, [](lv_event_t* event) {
        const auto* feedback = static_cast<const lv_event_code_t*>(lv_event_get_param(event));
        if (*feedback != LV_EVENT_PRESSED) return;
        auto* fixture = static_cast<GamepadWidget_RouterHandlesDeletionInsidePressFeedback_Test*>(lv_event_get_user_data(event));
        lv_obj_delete(fixture->button);
        fixture->type->destroyUI(&fixture->state);
        fixture->type = nullptr;
        fixture->button = nullptr;
    }, LV_EVENT_VALUE_CHANGED, this);
    GamepadTouchRouter router;
    TouchSnapshot snapshot;
    snapshot.count = 1;
    snapshot.contacts[0].horizontal = snapshot.contacts[0].vertical = 50;
    EXPECT_FALSE(router.update(snapshot, false, widget_generation).pressed);
    EXPECT_TRUE(router.reset_navigation);
    EXPECT_EQ(delivered().buttons, 0);
    EXPECT_FALSE(router.update(snapshot, false, widget_generation).pressed);
    snapshot.count = 0;
    router.update(snapshot, false, widget_generation);
}

TEST_F(GamepadWidget, RouterSkipsStationaryMovesButStillCancelsHiddenTargets) {
    create("gamepad_stick");
    auto* joystick = reinterpret_cast<GamepadJoystickState*>(state.data);
    auto original = joystick->touch.handler;
    unsigned moves = 0;
    struct HandlerContext {
        GamepadJoystickState* state;
        GamepadSurfaceTouch::PointHandler original;
        unsigned* moves;
    } context{joystick, original, &moves};
    joystick->touch.context = &context;
    joystick->touch.handler = [](void* opaque, GamepadTouchEvent interaction, const lv_point_t& point) {
        auto* handler = static_cast<HandlerContext*>(opaque);
        if (interaction == GamepadTouchEvent::Move) ++*handler->moves;
        handler->original(handler->state, interaction, point);
    };
    GamepadTouchRouter router;
    TouchSnapshot snapshot;
    snapshot.count = 1;
    snapshot.contacts[0].horizontal = snapshot.contacts[0].vertical = 50;
    router.update(snapshot, false, widget_generation);
    const uint32_t owner = joystick->touch.owner;
    ASSERT_NE(owner, 0U);
    touch_metadata_scans = 0;
    for (unsigned index = 0; index < 5; ++index) router.update(snapshot, false, widget_generation);
    EXPECT_EQ(moves, 0U);
    EXPECT_EQ(touch_metadata_scans, 5U);
    EXPECT_EQ(joystick->touch.owner, owner);
    snapshot.contacts[0].horizontal = 60;
    touch_metadata_scans = 0;
    router.update(snapshot, false, widget_generation);
    EXPECT_EQ(moves, 1U);
    EXPECT_EQ(touch_metadata_scans, 1U);
    lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    router.update(snapshot, false, widget_generation);
    EXPECT_EQ(joystick->touch.owner, 0U);
    EXPECT_TRUE(router.reset_navigation);
    joystick->touch.context = joystick;
    joystick->touch.handler = original;
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

TEST_F(GamepadWidget, NullInputResetReleasesAllInputDevicesSafely) {
    create("gamepad_button");
    read(true);
    EXPECT_EQ(delivered().buttons, 1);
    lv_indev_reset(nullptr, nullptr);
    EXPECT_EQ(reinterpret_cast<GamepadButtonState*>(state.data)->touch.owner, 0U);
    EXPECT_EQ(delivered().buttons, 0);
    EXPECT_EQ(ordinary_actions, 0U);
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

static TouchSnapshot contacts(std::initializer_list<TouchContact> values) {
    TouchSnapshot snapshot;
    for (const auto& contact : values) snapshot.contacts[snapshot.count++] = contact;
    return snapshot;
}

TEST_F(GamepadWidget, RouterRetainsIdsAndDoesNotAdoptExtraFinger) {
    create("gamepad_button");
    GamepadTouchRouter router;
    EXPECT_FALSE(router.update(contacts({{3, 50, 50}, {7, 100, 100}}), false, widget_generation).pressed);
    EXPECT_EQ(delivered().buttons, 1);
    router.update(contacts({{7, 100, 100}, {3, 250, 250}}), false, widget_generation);
    EXPECT_EQ(reinterpret_cast<GamepadButtonState*>(state.data)->touch.owner != 0, true);
    router.update(contacts({{7, 100, 100}}), false, widget_generation);
    EXPECT_EQ(delivered().buttons, 0);
    router.update(contacts({{7, 50, 50}}), false, widget_generation);
    EXPECT_EQ(reinterpret_cast<GamepadButtonState*>(state.data)->touch.owner, 0U);
    router.update(contacts({}), false, widget_generation);
    router.update(contacts({{7, 50, 50}}), false, widget_generation);
    EXPECT_EQ(delivered().buttons, 1);
    router.cancel();
}

TEST_F(GamepadWidget, RouterIdleGenerationChangePreservesFirstPress) {
    GamepadTouchRouter router;
    router.update(contacts({}), false, widget_generation, 1);
    router.cancel(false);
    auto idle = contacts({});
    idle.status = TouchReadStatus::Unchanged;
    router.update(idle, false, widget_generation + 1, 1);
    EXPECT_TRUE(router.update(contacts({{1, 250, 250}}), false, widget_generation + 1, 1).pressed);
    router.update(contacts({}), false, widget_generation + 1, 1);
    EXPECT_TRUE(router.update(contacts({{1, 250, 250}}), false, widget_generation + 1, 2).pressed);
    router.cancel();
}

TEST_F(GamepadWidget, RouterBootResetAndGenerationChangeDeliverFirstLvglClick) {
    GamepadTouchRouter router;
    router.update(contacts({}), false, widget_generation, 1);
    router.cancel(false);
    lv_indev_reset(indev, nullptr);
    struct RoutedInput {
        bool* pressed;
        lv_point_t* point;
        GamepadTouchRouter* router;
        uint32_t generation;
    } input{&pressed, &point, &router, widget_generation + 1};
    lv_indev_set_user_data(indev, &input);
    lv_indev_set_read_cb(indev, [](lv_indev_t* device, lv_indev_data_t* data) {
        auto* context = static_cast<RoutedInput*>(lv_indev_get_user_data(device));
        TouchSnapshot snapshot;
        snapshot.count = *context->pressed ? 1 : 0;
        snapshot.contacts[0] = {1, uint16_t(context->point->x), uint16_t(context->point->y)};
        const TouchSample navigation = context->router->update(snapshot, false, context->generation, 1);
        if (context->router->reset_navigation) lv_indev_reset(device, nullptr);
        data->state = navigation.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->point = {navigation.horizontal, navigation.vertical};
    });
    lv_obj_add_event_cb(button, [](lv_event_t* event) {
        ++*static_cast<unsigned*>(lv_event_get_user_data(event));
    }, LV_EVENT_CLICKED, &ordinary_actions);
    lv_obj_update_layout(button);
    read(true);
    EXPECT_TRUE(lv_obj_has_state(button, LV_STATE_PRESSED));
    read(false);
    EXPECT_EQ(ordinary_actions, 1U);
    lv_indev_set_read_cb(indev, nullptr);
    lv_indev_set_user_data(indev, this);
    router.cancel();
}

TEST_F(GamepadWidget, RouterHeldGenerationChangeRequiresFreshRelease) {
    GamepadTouchRouter router;
    EXPECT_TRUE(router.update(contacts({{1, 250, 250}}), false, widget_generation, 1).pressed);
    EXPECT_FALSE(router.update(contacts({{1, 250, 250}}), false, widget_generation + 1, 1).pressed);
    auto idle = contacts({});
    idle.status = TouchReadStatus::Unchanged;
    router.update(idle, false, widget_generation + 1, 2);
    router.cancel(false);
    EXPECT_FALSE(router.update(contacts({{1, 250, 250}}), false, widget_generation + 1, 2).pressed);
    router.update(contacts({}), false, widget_generation + 1, 2);
    EXPECT_TRUE(router.update(contacts({{1, 250, 250}}), false, widget_generation + 1, 2).pressed);
    router.cancel();
}

TEST_F(GamepadWidget, RouterGenerationChangeDuringErrorRequiresFreshRelease) {
    GamepadTouchRouter router;
    router.update(contacts({}), false, widget_generation, 1);
    auto error = contacts({});
    error.status = TouchReadStatus::Error;
    router.update(error, false, widget_generation + 1, 1);
    router.cancel(false);
    EXPECT_FALSE(router.update(contacts({{1, 250, 250}}), false, widget_generation + 1, 1).pressed);
    router.update(contacts({}), false, widget_generation + 1, 1);
    EXPECT_TRUE(router.update(contacts({{1, 250, 250}}), false, widget_generation + 1, 1).pressed);
    router.cancel();
}

TEST_F(GamepadWidget, RouterCancellationRequiresFreshAllReleasedSnapshot) {
    create("gamepad_stick");
    GamepadTouchRouter router;
    router.update(contacts({{2, 175, 100}}), false, widget_generation);
    EXPECT_EQ(delivered().left_x, 32767);
    router.update(contacts({{2, 175, 100}}), true, widget_generation);
    EXPECT_EQ(delivered().left_x, 0);
    router.update(contacts({{2, 175, 100}}), false, widget_generation);
    EXPECT_EQ(reinterpret_cast<GamepadJoystickState*>(state.data)->touch.owner, 0U);
    auto unchanged = contacts({});
    unchanged.status = TouchReadStatus::Unchanged;
    router.update(unchanged, false, widget_generation);
    router.update(contacts({{2, 175, 100}}), false, widget_generation);
    EXPECT_EQ(reinterpret_cast<GamepadJoystickState*>(state.data)->touch.owner, 0U);
    router.update(contacts({}), false, widget_generation);
    router.update(contacts({{2, 175, 100}}), false, widget_generation);
    EXPECT_EQ(delivered().left_x, 32767);
    router.cancel();
}

TEST_F(GamepadWidget, RouterHideShowInvalidatesCaptureWithoutRecapture) {
    create("gamepad_button");
    GamepadTouchRouter router;
    router.update(contacts({{1, 50, 50}}), false, widget_generation);
    EXPECT_EQ(delivered().buttons, 1);
    type->onHide(&state);
    type->onShow(&state);
    router.update(contacts({{1, 50, 50}}), false, widget_generation);
    EXPECT_EQ(delivered().buttons, 0);
    router.update(contacts({{1, 50, 50}}), false, widget_generation);
    EXPECT_EQ(reinterpret_cast<GamepadButtonState*>(state.data)->touch.owner, 0U);
}

TEST_F(GamepadWidget, RouterControllerLocksNavigationUntilEveryFingerLifts) {
    create("gamepad_button");
    GamepadTouchRouter router;
    EXPECT_TRUE(router.update(contacts({{1, 250, 250}}), false, widget_generation).pressed);
    EXPECT_FALSE(router.update(contacts({{1, 250, 250}, {2, 50, 50}}), false, widget_generation).pressed);
    EXPECT_TRUE(router.reset_navigation);
    EXPECT_FALSE(router.update(contacts({{1, 250, 250}}), false, widget_generation).pressed);
    EXPECT_FALSE(router.update(contacts({{1, 250, 250}, {4, 240, 240}}), false, widget_generation).pressed);
    router.update(contacts({}), false, widget_generation);
    EXPECT_TRUE(router.update(contacts({{4, 240, 240}}), false, widget_generation).pressed);
}

TEST_F(GamepadWidget, RouterDoesNotPromoteHeldNavigationOrMalformedIds) {
    GamepadTouchRouter router;
    EXPECT_TRUE(router.update(contacts({{2, 250, 250}, {3, 240, 240}}), false, widget_generation).pressed);
    EXPECT_FALSE(router.update(contacts({{3, 240, 240}}), false, widget_generation).pressed);
    EXPECT_TRUE(router.update(contacts({{3, 240, 240}, {4, 230, 230}}), false, widget_generation).pressed);
    EXPECT_FALSE(router.update(contacts({{4, 230, 230}, {4, 50, 50}}), false, widget_generation).pressed);
    EXPECT_TRUE(router.reset_navigation);
}

TEST_F(GamepadWidget, RouterTwoSticksRemainIndependentAcrossReorderCrossingAndLift) {
    lv_obj_set_size(button, 150, 150);
    create("gamepad_stick");
    create_extra("gamepad_stick");
    GamepadTouchRouter router;
    router.update(contacts({{1, 125, 75}, {8, 275, 75}}), false, widget_generation);
    auto report = delivered();
    EXPECT_EQ(report.left_x, 32767);
    EXPECT_EQ(report.right_x, 32767);
    const auto left_owner = reinterpret_cast<GamepadJoystickState*>(state.data)->touch.owner;
    const auto right_owner = reinterpret_cast<GamepadJoystickState*>(extra_state.data)->touch.owner;
    ASSERT_NE(left_owner, 0U);
    ASSERT_NE(right_owner, 0U);
    router.update(contacts({{8, 25, 75}, {1, 275, 75}}), false, widget_generation);
    report = delivered();
    EXPECT_EQ(report.left_x, 32767);
    EXPECT_EQ(report.right_x, -32767);
    EXPECT_EQ(reinterpret_cast<GamepadJoystickState*>(state.data)->touch.owner, left_owner);
    EXPECT_EQ(reinterpret_cast<GamepadJoystickState*>(extra_state.data)->touch.owner, right_owner);
    router.update(contacts({{8, 25, 75}}), false, widget_generation);
    report = delivered();
    EXPECT_EQ(report.left_x, 0);
    EXPECT_EQ(report.right_x, -32767);
    router.cancel();
    EXPECT_EQ(delivered().right_x, 0);
}

TEST_F(GamepadWidget, RouterNavigationEmitsReleaseBeforeNewContactPress) {
    GamepadTouchRouter router;
    EXPECT_TRUE(router.update(contacts({{1, 250, 250}}), false, widget_generation).pressed);
    EXPECT_FALSE(router.update(contacts({{2, 240, 240}}), false, widget_generation).pressed);
    auto unchanged = contacts({{2, 240, 240}});
    unchanged.status = TouchReadStatus::Unchanged;
    const auto navigation = router.update(unchanged, false, widget_generation);
    EXPECT_TRUE(navigation.pressed);
    EXPECT_EQ(navigation.horizontal, 240);
}

TEST_F(GamepadWidget, RouterHeldTriggerCoexistsWithStickAndFeedback) {
    lv_obj_set_size(button, 150, 150);
    create("gamepad_stick");
    create_extra("gamepad_button", {GamepadControlKind::Trigger, 0});
    GamepadTouchRouter router;
    router.update(contacts({{1, 125, 75}, {2, 225, 75}}), false, widget_generation);
    auto report = delivered();
    EXPECT_EQ(report.left_x, 32767);
    EXPECT_EQ(report.left_trigger, 255);
    EXPECT_TRUE(lv_obj_has_state(extra_button, LV_STATE_PRESSED));
    router.update(contacts({{2, 225, 75}}), false, widget_generation);
    report = delivered();
    EXPECT_EQ(report.left_x, 0);
    EXPECT_EQ(report.left_trigger, 255);
    router.cancel();
    EXPECT_EQ(delivered().left_trigger, 0);
    EXPECT_FALSE(lv_obj_has_state(extra_button, LV_STATE_PRESSED));
}

TEST_F(GamepadWidget, RouterHatDirectionsComposeAndReleaseIndependently) {
    lv_obj_set_size(button, 150, 150);
    button_config.actions[0].payload.gamepad = {uint8_t(GamepadControlKind::Hat), 0, 1};
    create("gamepad_button");
    create_extra("gamepad_button", {GamepadControlKind::Hat, 3});
    GamepadTouchRouter router;
    router.update(contacts({{4, 75, 75}, {9, 225, 75}}), false, widget_generation);
    EXPECT_EQ(delivered().hat, 2);
    router.update(contacts({{9, 225, 75}}), false, widget_generation);
    EXPECT_EQ(delivered().hat, 3);
    router.cancel();
    EXPECT_EQ(delivered().hat, 0);
}

TEST_F(GamepadWidget, RouterTransportResetAndDestroyedTargetRequireAllRelease) {
    create("gamepad_button");
    GamepadTouchRouter router;
    router.update(contacts({{5, 50, 50}}), false, widget_generation);
    delivered();
    widget_controller.reset();
    ++widget_generation;
    router.update(contacts({{5, 50, 50}}), false, widget_generation);
    EXPECT_EQ(reinterpret_cast<GamepadButtonState*>(state.data)->touch.owner, 0U);
    router.update(contacts({{5, 50, 50}}), false, widget_generation);
    EXPECT_EQ(reinterpret_cast<GamepadButtonState*>(state.data)->touch.owner, 0U);
    router.update(contacts({}), false, widget_generation);
    router.update(contacts({{5, 50, 50}}), false, widget_generation);
    EXPECT_EQ(delivered().buttons, 1);
    type->destroyUI(&state);
    type = nullptr;
    lv_obj_delete(button);
    button = nullptr;
    EXPECT_FALSE(router.update(contacts({{5, 50, 50}}), false, widget_generation).pressed);
    EXPECT_TRUE(router.reset_navigation);
    EXPECT_EQ(delivered().buttons, 0);
}

TEST_F(GamepadWidget, RouterTranslatedJoystickUsesLogicalCoordinatesAndTopmostObject) {
    create("gamepad_stick");
    lv_obj_set_style_translate_x(button, 30, LV_PART_MAIN);
    lv_obj_update_layout(button);
    GamepadTouchRouter router;
    router.update(contacts({{1, 130, 100}}), false, widget_generation);
    EXPECT_EQ(delivered().left_x, 0);
    router.update(contacts({{1, 205, 100}}), false, widget_generation);
    EXPECT_EQ(delivered().left_x, 32767);
    router.update(contacts({}), false, widget_generation);
    auto* overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, 300, 300);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_update_layout(overlay);
    EXPECT_TRUE(router.update(contacts({{1, 130, 100}}), false, widget_generation).pressed);
    EXPECT_EQ(reinterpret_cast<GamepadJoystickState*>(state.data)->touch.owner, 0U);
    router.cancel();
}

TEST_F(GamepadWidget, ObjectDeletionBeforeWidgetStateCancelsAndAllowsSafeCleanup) {
    create("gamepad_stick");
    GamepadTouchRouter router;
    router.update(contacts({{6, 175, 100}}), false, widget_generation);
    EXPECT_EQ(delivered().left_x, 32767);
    lv_obj_delete(button);
    button = nullptr;
    EXPECT_EQ(delivered().left_x, 0);
    EXPECT_FALSE(router.update(contacts({{6, 175, 100}}), false, widget_generation).pressed);
    gamepad_stick_tick(nullptr, &config, &state);
    type->destroyUI(&state);
    type = nullptr;
}

TEST_F(GamepadWidget, MouseButtonStripRepositionsPointerAndPreservesRoles) {
    create("mousepad", true);
    mouse_reports();
    GamepadTouchRouter router;
    router.update(contacts({{4, 50, 180}}), false, widget_generation);
    auto reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].buttons, 1);
    router.update(contacts({{4, 150, 20}, {1, 50, 50}}), false, widget_generation);
    router.update(contacts({{4, 150, 20}, {1, 80, 180}, {7, 90, 50}}), false, widget_generation);
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 2U);
    EXPECT_EQ(reports[0].buttons, 1);
    EXPECT_EQ(reports[0].dx, 30);
    EXPECT_EQ(reports[0].wheel, 0);
    router.update(contacts({{4, 150, 20}, {7, 90, 50}}), false, widget_generation);
    EXPECT_TRUE(mouse_reports().empty());
    router.update(contacts({{4, 150, 20}, {7, 120, 50}, {2, 100, 50}}), false, widget_generation);
    router.update(contacts({{4, 150, 20}, {7, 130, 50}, {2, 110, 50}}), false, widget_generation);
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].dx, 10);
    EXPECT_EQ(reports[0].buttons, 1);
    router.update(contacts({{7, 130, 50}, {2, 110, 50}}), false, widget_generation);
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].buttons, 0);
    router.update(contacts({}), false, widget_generation);
    EXPECT_TRUE(mouse_reports().empty());
}

TEST_F(GamepadWidget, MouseButtonStripPointerFirstRightHoldAndSingleContactClick) {
    create("mousepad", true);
    mouse_reports();
    GamepadTouchRouter router;
    router.update(contacts({{4, 50, 50}}), false, widget_generation);
    router.update(contacts({{4, 50, 50}, {1, 150, 180}}), false, widget_generation);
    router.update(contacts({{1, 150, 180}, {4, 70, 50}}), false, widget_generation);
    auto reports = mouse_reports();
    ASSERT_EQ(reports.size(), 2U);
    EXPECT_EQ(reports[0].buttons, 2);
    EXPECT_EQ(reports[1].buttons, 2);
    EXPECT_EQ(reports[1].dx, 20);
    router.update(contacts({{4, 70, 50}}), false, widget_generation);
    router.update(contacts({{4, 80, 50}}), false, widget_generation);
    router.update(contacts({}), false, widget_generation);
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 2U);
    EXPECT_EQ(reports[0].buttons, 0);
    EXPECT_EQ(reports[1].buttons, 0);
    EXPECT_EQ(reports[1].dx, 10);
    point = {150, 180};
    read(true);
    read(false);
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 2U);
    EXPECT_EQ(reports[0].buttons, 2);
    EXPECT_EQ(reports[1].buttons, 0);
    EXPECT_EQ(ordinary_actions, 0U);
}

TEST_F(GamepadWidget, MouseButtonStripUsesTranslatedSurfaceBounds) {
    create("mousepad", true);
    lv_obj_set_pos(button, 50, 60);
    lv_obj_update_layout(button);
    mouse_reports();
    GamepadTouchRouter router;
    router.update(contacts({{4, 70, 180}}), false, widget_generation);
    EXPECT_TRUE(mouse_reports().empty());
    router.update(contacts({{4, 70, 180}, {1, 120, 240}}), false, widget_generation);
    router.update(contacts({{4, 80, 180}, {1, 120, 240}}), false, widget_generation);
    auto reports = mouse_reports();
    ASSERT_EQ(reports.size(), 2U);
    EXPECT_EQ(reports[0].buttons, 1);
    EXPECT_EQ(reports[1].buttons, 1);
    EXPECT_EQ(reports[1].dx, 10);
    router.update(contacts({}), false, widget_generation);
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].buttons, 0);
    point = {220, 240};
    read(true);
    read(false);
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 2U);
    EXPECT_EQ(reports[0].buttons, 2);
    EXPECT_EQ(reports[1].buttons, 0);
}

TEST_F(GamepadWidget, MouseButtonStripCancellationAlwaysReleases) {
    create("mousepad", true);
    GamepadTouchRouter router;
    auto begin_hold = [&] {
        router.update(contacts({}), false, widget_generation, mouse_hid_generation());
        router.update(contacts({{4, 50, 180}}), false, widget_generation, mouse_hid_generation());
        ASSERT_TRUE(reinterpret_cast<MousepadState*>(state.data)->touch.allow_replacement);
        mouse_reports();
    };
    auto released = [&] {
        auto* mouse = reinterpret_cast<MousepadState*>(state.data);
        EXPECT_EQ(mouse->owner, 0U);
        EXPECT_FALSE(mouse->touch.allow_replacement);
        const auto reports = mouse_reports();
        ASSERT_EQ(reports.size(), 1U);
        EXPECT_EQ(reports[0].buttons, 0);
    };
    begin_hold();
    type->onHide(&state);
    type->onShow(&state);
    router.update(contacts({{4, 50, 180}}), false, widget_generation, mouse_hid_generation());
    released();
    begin_hold();
    ++widget_mouse_epoch;
    widget_mouse.reset();
    router.update(contacts({{4, 50, 180}}), false, widget_generation, mouse_hid_generation());
    released();
    begin_hold();
    router.update(contacts({{4, 50, 180}}), true, widget_generation, mouse_hid_generation());
    released();
    begin_hold();
    lv_obj_delete(button);
    button = nullptr;
    released();
}

TEST_F(GamepadWidget, MouseButtonStripRendersUnfilledDashedRoundedTextColorOutlines) {
    create("mousepad", true);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, lv_color_hex(0x12AB34), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    std::vector<uint8_t> pixels(300 * 300 * 3);
    lv_display_set_user_data(display, &pixels);
    lv_display_set_flush_cb(display, [](lv_display_t* output, const lv_area_t* area, uint8_t* data) {
        auto& image = *static_cast<std::vector<uint8_t>*>(lv_display_get_user_data(output));
        const int row_bytes = lv_area_get_width(area) * 3;
        for (int row = area->y1; row <= area->y2; ++row)
            std::copy(data + (row - area->y1) * row_bytes,
                      data + (row - area->y1 + 1) * row_bytes,
                      image.begin() + (row * 300 + area->x1) * 3);
        lv_display_flush_ready(output);
    });
    lv_refr_now(display);
    auto foreground = [&](int x, int y) {
        const auto offset = (y * 300 + x) * 3;
        return pixels[offset] == 0x34 && pixels[offset + 1] == 0xAB && pixels[offset + 2] == 0x12;
    };
    unsigned dashes = 0, gaps = 0;
    for (int x = 12; x < 88; ++x) {
        if (foreground(x, 163)) ++dashes;
        else ++gaps;
    }
    EXPECT_GT(dashes, 20U);
    EXPECT_GT(gaps, 10U);
    EXPECT_FALSE(foreground(3, 163));
    EXPECT_FALSE(foreground(50, 180));
    EXPECT_FALSE(foreground(150, 180));
    EXPECT_FALSE(foreground(50, 100));
    unsigned right_dashes = 0;
    for (int x = 112; x < 188; ++x) right_dashes += foreground(x, 163);
    EXPECT_EQ(right_dashes, dashes);
    mouse_reports();
    GamepadTouchRouter router;
    router.update(contacts({{4, 50, 180}}), false, widget_generation);
    lv_refr_now(display);
    unsigned pressed_thickness = 0, normal_thickness = 0;
    for (int x = 12; x < 88; ++x) {
        pressed_thickness += foreground(x, 164);
        normal_thickness += foreground(x + 100, 164);
    }
    EXPECT_GT(pressed_thickness, normal_thickness);
    EXPECT_FALSE(foreground(50, 180));
    router.cancel();
    lv_display_set_user_data(display, nullptr);
    lv_display_set_flush_cb(display, [](lv_display_t* output, const lv_area_t*, uint8_t*) { lv_display_flush_ready(output); });
}

TEST_F(GamepadWidget, MouseCaptureDoesNotRescanMetadata) {
    create("mousepad");
    GamepadTouchRouter router;
    const auto snapshot = contacts({{1, 50, 100}, {4, 120, 100}});
    router.update(snapshot, false, widget_generation);
    touch_metadata_scans = 0;
    for (unsigned index = 0; index < 5; ++index) router.update(snapshot, false, widget_generation);
    router.update(contacts({{1, 50, 60}, {4, 120, 60}}), false, widget_generation);
    router.update(contacts({{1, 50, 60}}), false, widget_generation);
    router.update(contacts({}), false, widget_generation);
    EXPECT_EQ(touch_metadata_scans, 0U);
    router.update(contacts({{1, 50, 100}}), false, widget_generation);
    EXPECT_GT(touch_metadata_scans, 0U);
    router.cancel();
}

TEST_F(GamepadWidget, MouseRouterFreshBaselineReorderAndOutsideCapture) {
    create("mousepad");
    mouse_reports();
    GamepadTouchRouter router;
    EXPECT_FALSE(router.update(contacts({{4, 50, 100}}), false, widget_generation).pressed);
    EXPECT_TRUE(router.reset_navigation);
    router.update(contacts({{1, 120, 100}, {4, 90, 100}}), false, widget_generation);
    EXPECT_TRUE(mouse_reports().empty());
    lv_tick_inc(20);
    router.update(contacts({{4, 90, 60}, {1, 120, 60}}), false, widget_generation);
    auto reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].wheel, 2);
    EXPECT_EQ(reports[0].dx, 0);
    router.update(contacts({{1, 250, 20}, {4, 250, 20}}), false, widget_generation);
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].wheel, 2);
    EXPECT_EQ(reports[0].pan, 0);
    router.update(contacts({{4, 250, 20}}), false, widget_generation);
    router.update(contacts({{4, 280, 50}, {3, 100, 100}}), false, widget_generation);
    router.update(contacts({}), false, widget_generation);
    EXPECT_TRUE(mouse_reports().empty());
}

TEST_F(GamepadWidget, MouseRouterExtraFingerIsNeverPromoted) {
    create("mousepad");
    mouse_reports();
    GamepadTouchRouter router;
    router.update(contacts({{1, 50, 100}, {4, 120, 100}, {7, 150, 100}}), false, widget_generation);
    router.update(contacts({{7, 150, 20}, {4, 120, 60}, {1, 50, 60}}), false, widget_generation);
    ASSERT_EQ(mouse_reports().size(), 1U);
    router.update(contacts({{7, 150, 20}, {1, 50, 60}}), false, widget_generation);
    router.update(contacts({{7, 150, 60}, {1, 50, 20}}), false, widget_generation);
    router.update(contacts({}), false, widget_generation);
    EXPECT_TRUE(mouse_reports().empty());
}

TEST_F(GamepadWidget, MouseRouterSingleContactAndSyntheticTapShareHandlers) {
    create("mousepad");
    mouse_reports();
    GamepadTouchRouter router;
    router.update(contacts({{0, 50, 50}}), false, widget_generation);
    router.update(contacts({{0, 60, 40}}), false, widget_generation);
    auto reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].dx, 10);
    EXPECT_EQ(reports[0].dy, -10);
    router.update(contacts({}), false, widget_generation);
    EXPECT_TRUE(mouse_reports().empty());
    router.update(contacts({{0, 50, 50}}), false, widget_generation);
    lv_tick_inc(20);
    router.update(contacts({}), false, widget_generation);
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 2U);
    EXPECT_EQ(reports[0].buttons, 1);
    EXPECT_EQ(reports[1].buttons, 0);
    read(true);
    read(false);
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 2U);
    EXPECT_EQ(reports[0].buttons, 1);
    EXPECT_EQ(ordinary_actions, 0U);
    EXPECT_EQ(swipe_actions, 0U);
}

TEST_F(GamepadWidget, MouseRouterOnlyOneSurfaceButGamepadCanCoexist) {
    create("mousepad");
    create_extra("gamepad_button");
    mouse_reports();
    GamepadTouchRouter router;
    router.update(contacts({{1, 50, 50}, {2, 250, 50}}), false, widget_generation);
    EXPECT_EQ(delivered().buttons, 2);
    router.update(contacts({{1, 70, 50}, {2, 250, 50}}), false, widget_generation);
    auto reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].dx, 20);
    router.update(contacts({{1, 70, 50}}), false, widget_generation);
    EXPECT_EQ(delivered().buttons, 0);
    router.cancel();
}

TEST_F(GamepadWidget, MouseRouterRejectsSecondSurfaceAndHeldOutsideFinger) {
    create("mousepad");
    create_extra("scrollpad");
    mouse_reports();
    GamepadTouchRouter router;
    router.update(contacts({{1, 50, 100}, {2, 250, 100}, {3, 280, 250}}), false, widget_generation);
    router.update(contacts({{1, 70, 100}, {2, 250, 60}, {3, 100, 100}}), false, widget_generation);
    auto reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].dx, 20);
    EXPECT_EQ(reports[0].wheel, 0);
    router.cancel();
}

TEST_F(GamepadWidget, MouseRouterCancellationRequiresFreshRawRelease) {
    create("mousepad");
    mouse_reports();
    GamepadTouchRouter router;
    router.update(contacts({{1, 50, 50}}), false, widget_generation, mouse_hid_generation());
    type->onHide(&state);
    type->onShow(&state);
    router.update(contacts({{1, 60, 50}}), false, widget_generation, mouse_hid_generation());
    mouse_reports();
    TouchSnapshot stale;
    stale.status = TouchReadStatus::Unchanged;
    router.update(stale, false, widget_generation, mouse_hid_generation());
    router.update(contacts({{2, 100, 100}}), false, widget_generation, mouse_hid_generation());
    EXPECT_TRUE(mouse_reports().empty());
    router.update(contacts({}), false, widget_generation, mouse_hid_generation());
    router.update(contacts({{2, 100, 100}}), false, widget_generation, mouse_hid_generation());
    router.update(contacts({}), false, widget_generation, mouse_hid_generation());
    auto reports = mouse_reports();
    ASSERT_EQ(reports.size(), 2U);
    EXPECT_EQ(reports[0].buttons, 1);
}

TEST_F(GamepadWidget, ScrollpadRemainsSingleContactWithCapture) {
    create("scrollpad");
    mouse_reports();
    GamepadTouchRouter router;
    router.update(contacts({{1, 50, 100}, {4, 100, 100}}), false, widget_generation);
    router.update(contacts({{1, 250, 60}, {4, 100, 20}}), false, widget_generation);
    auto reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].wheel, 2);
    router.update(contacts({{4, 100, 20}}), false, widget_generation);
    router.update(contacts({{4, 100, 80}}), false, widget_generation);
    router.update(contacts({}), false, widget_generation);
    EXPECT_TRUE(mouse_reports().empty());
}

TEST_F(GamepadWidget, MouseDragOwnsFingerAndSuppressesExtraFingers) {
    create("mousepad");
    mouse_reports();
    GamepadTouchRouter router;
    router.update(contacts({{4, 50, 50}}), false, widget_generation);
    lv_tick_inc(20);
    router.update(contacts({}), false, widget_generation);
    router.update(contacts({{7, 50, 50}}), false, widget_generation);
    router.update(contacts({{7, 80, 50}}), false, widget_generation);
    auto* mouse = reinterpret_cast<MousepadState*>(state.data);
    ASSERT_NE(mouse->owner, 0U);
    auto reports = mouse_reports();
    ASSERT_EQ(reports.size(), 4U);
    EXPECT_EQ(reports[0].buttons, 1);
    EXPECT_EQ(reports[1].buttons, 0);
    EXPECT_EQ(reports[2].buttons, 1);
    EXPECT_EQ(reports[3].buttons, 1);
    EXPECT_EQ(reports[3].dx, 30);
    router.update(contacts({{7, 90, 50}, {1, 100, 100}, {2, 120, 100}}), false, widget_generation);
    router.update(contacts({{7, 100, 50}, {1, 100, 20}, {2, 120, 20}}), false, widget_generation);
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].buttons, 1);
    EXPECT_EQ(reports[0].dx, 20);
    EXPECT_EQ(reports[0].wheel, 0);
    router.update(contacts({{1, 100, 20}, {2, 120, 20}}), false, widget_generation);
    EXPECT_EQ(mouse->owner, 0U);
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].buttons, 0);
    router.update(contacts({{1, 150, 100}, {2, 170, 100}}), false, widget_generation);
    EXPECT_TRUE(mouse_reports().empty());
    router.update(contacts({}), false, widget_generation);
}

TEST_F(GamepadWidget, SyntheticReleaseConsumesFinalPosition) {
    create("mousepad");
    mouse_reports();
    read(true);
    point.x = 80;
    read(false);
    const auto reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].dx, 30);
    EXPECT_EQ(reports[0].buttons, 0);
}

TEST_F(GamepadWidget, SyntheticTapDragAndDoubleClick) {
    create("mousepad");
    mouse_reports();
    read(true);
    read(false);
    read(true);
    read(false);
    auto reports = mouse_reports();
    ASSERT_EQ(reports.size(), 4U);
    EXPECT_EQ(reports[0].buttons, 1);
    EXPECT_EQ(reports[1].buttons, 0);
    EXPECT_EQ(reports[2].buttons, 1);
    EXPECT_EQ(reports[3].buttons, 0);
    read(true);
    read(false);
    read(true);
    point.x = 80;
    read(true);
    ASSERT_NE(reinterpret_cast<MousepadState*>(state.data)->owner, 0U);
    read(false);
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 5U);
    EXPECT_EQ(reports[2].buttons, 1);
    EXPECT_EQ(reports[3].buttons, 1);
    EXPECT_EQ(reports[3].dx, 30);
    EXPECT_EQ(reports[4].buttons, 0);
}

TEST_F(GamepadWidget, MouseDragCancellationHideDeleteDisconnectAndOta) {
    create("mousepad");
    GamepadTouchRouter router;
    auto begin_drag = [&] {
        router.update(contacts({}), false, widget_generation, mouse_hid_generation());
        router.update(contacts({{4, 50, 50}}), false, widget_generation, mouse_hid_generation());
        router.update(contacts({}), false, widget_generation, mouse_hid_generation());
        router.update(contacts({{7, 50, 50}}), false, widget_generation, mouse_hid_generation());
        router.update(contacts({{7, 80, 50}}), false, widget_generation, mouse_hid_generation());
        ASSERT_NE(reinterpret_cast<MousepadState*>(state.data)->owner, 0U);
        mouse_reports();
    };
    begin_drag();
    type->onHide(&state);
    type->onShow(&state);
    EXPECT_EQ(reinterpret_cast<MousepadState*>(state.data)->owner, 0U);
    router.update(contacts({{7, 90, 50}}), false, widget_generation, mouse_hid_generation());
    auto reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].buttons, 0);
    begin_drag();
    ++widget_mouse_epoch;
    widget_mouse.reset();
    router.update(contacts({{7, 90, 50}}), false, widget_generation, mouse_hid_generation());
    EXPECT_EQ(reinterpret_cast<MousepadState*>(state.data)->owner, 0U);
    mouse_reports();
    begin_drag();
    router.update(contacts({{7, 90, 50}}), true, widget_generation, mouse_hid_generation());
    EXPECT_EQ(reinterpret_cast<MousepadState*>(state.data)->owner, 0U);
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].buttons, 0);
    begin_drag();
    lv_obj_delete(button);
    button = nullptr;
    EXPECT_EQ(reinterpret_cast<MousepadState*>(state.data)->owner, 0U);
    router.update(contacts({{7, 90, 50}}), false, widget_generation, mouse_hid_generation());
    reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].buttons, 0);
}

TEST_F(GamepadWidget, MouseDisconnectBetweenTapsClearsDragCandidate) {
    create("mousepad");
    mouse_reports();
    read(true);
    read(false);
    mouse_reports();
    widget_mouse.reset();
    ++widget_mouse_epoch;
    mouse_reports();
    read(true);
    point.x = 80;
    read(true);
    EXPECT_EQ(reinterpret_cast<MousepadState*>(state.data)->owner, 0U);
    auto reports = mouse_reports();
    ASSERT_EQ(reports.size(), 1U);
    EXPECT_EQ(reports[0].buttons, 0);
    read(false);
}

TEST_F(GamepadWidget, MouseSurfaceHideStopsReleasedInertia) {
    create("scrollpad");
    auto* scrollpad = reinterpret_cast<ScrollpadState*>(state.data);
    scrollpad->config.inertia = 5;
    mouse_reports();
    GamepadTouchRouter router;
    router.update(contacts({{1, 50, 100}}), false, widget_generation);
    lv_tick_inc(20);
    router.update(contacts({{1, 50, 60}}), false, widget_generation);
    router.update(contacts({}), false, widget_generation);
    mouse_reports();
    EXPECT_TRUE(widget_mouse.inertia_tick(lv_tick_get() + 20).valid);
    type->onHide(&state);
    EXPECT_FALSE(widget_mouse.inertia_tick(lv_tick_get() + 20).valid);
}

TEST_F(GamepadWidget, PadSwipeHandlerCanBeDisabledAndReenabledWithoutDuplicates) {
    create("mousepad");
    widget_ready = false;
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, 320, 240);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    swipe_actions_register(lv_screen_active(), false);
    swipe_actions_register(overlay, false);
    point = {250, 60};
    read(true);
    point.y = 180;
    read(true);
    read(false);
    EXPECT_EQ(navigation_swipe_requests, 0U);
    swipe_actions_register(lv_screen_active());
    swipe_actions_register(lv_screen_active());
    swipe_actions_register(overlay);
    swipe_actions_register(overlay);
    lv_tick_inc(400);
    point = {250, 60};
    read(true);
    point.y = 180;
    read(true);
    read(false);
    EXPECT_EQ(navigation_swipe_requests, 1U);
    swipe_actions_register(lv_screen_active(), false);
    swipe_actions_register(overlay, false);
    lv_tick_inc(400);
    point = {250, 60};
    read(true);
    point.y = 180;
    read(true);
    read(false);
    EXPECT_EQ(navigation_swipe_requests, 1U);
}

TEST_F(GamepadWidget, MousepadBackIsOptInAndConsumesTapsWithoutUsb) {
    create("mousepad", false, true);
    ASSERT_EQ(lv_obj_get_child_count(button), 1U);
    lv_obj_t* back = lv_obj_get_child(button, 0);
    EXPECT_EQ(lv_obj_get_width(back), 44);
    EXPECT_EQ(lv_obj_get_height(back), 44);
    widget_ready = false;
    mouse_reports();
    point = {20, 20};
    read(true);
    read(false);
    EXPECT_EQ(widget_back_requests, 1U);
    EXPECT_EQ(ordinary_actions, 0U);
    EXPECT_EQ(swipe_actions, 0U);
    EXPECT_TRUE(mouse_reports().empty());
}

TEST_F(GamepadWidget, MousepadBackCancelsMovedAndLongPresses) {
    create("mousepad", false, true);
    mouse_reports();
    point = {20, 20};
    read(true);
    point = {80, 80};
    read(true);
    point = {20, 20};
    read(true);
    read(false);
    EXPECT_EQ(widget_back_requests, 0U);
    EXPECT_TRUE(mouse_reports().empty());
    read(true);
    lv_tick_inc(1000);
    read(true);
    read(false);
    EXPECT_EQ(widget_back_requests, 0U);
    EXPECT_TRUE(mouse_reports().empty());
}

TEST_F(GamepadWidget, MousepadRawBackAfterMouseInputDoesNotResetGeneration) {
    create("mousepad", false, true);
    struct RawInput {
        GamepadTouchRouter router;
        TouchSnapshot snapshot;
    } raw;
    lv_indev_set_user_data(indev, &raw);
    lv_indev_set_read_cb(indev, [](lv_indev_t* input, lv_indev_data_t* data) {
        auto* raw = static_cast<RawInput*>(lv_indev_get_user_data(input));
        const auto navigation = raw->router.update(raw->snapshot, false, widget_generation,
                                                   mouse_hid_generation());
        if (raw->router.reset_navigation) lv_indev_reset(input, nullptr);
        data->state = navigation.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        if (navigation.pressed) data->point = {navigation.horizontal, navigation.vertical};
    });
    raw.snapshot = contacts({{1, 80, 80}});
    read(true);
    raw.snapshot = contacts({{1, 110, 110}});
    read(true);
    raw.snapshot = contacts({});
    read(false);
    mouse_reports();
    const uint32_t generation = mouse_hid_generation();
    raw.snapshot = contacts({{1, 20, 20}});
    read(true);
    EXPECT_EQ(mouse_hid_generation(), generation);
    EXPECT_EQ(MouseSurfaceTouch::session(), nullptr);
    raw.snapshot = contacts({});
    read(false);
    EXPECT_EQ(widget_back_requests, 1U);
    EXPECT_TRUE(mouse_reports().empty());
}

TEST_F(GamepadWidget, MousepadRawMovementAcrossBackNeverNavigates) {
    create("mousepad", false, true);
    mouse_reports();
    GamepadTouchRouter router;
    router.update(contacts({{1, 80, 80}}), false, widget_generation, mouse_hid_generation());
    router.update(contacts({{1, 20, 20}}), false, widget_generation, mouse_hid_generation());
    router.update(contacts({}), false, widget_generation, mouse_hid_generation());
    EXPECT_EQ(widget_back_requests, 0U);
    EXPECT_FALSE(mouse_reports().empty());
}

TEST_F(GamepadWidget, MousepadAuthoringValidationAndParse) {
    const auto* mousepad = widget_find("mousepad");
    ASSERT_NE(mousepad->validateConfig, nullptr);
    JsonDocument document;
    EXPECT_EQ(mousepad->validateConfig(document.to<JsonObject>()), nullptr);
    for (const char* key : {"widget_mousepad_sensitivity", "widget_mousepad_acceleration",
                            "widget_mousepad_movement_threshold", "widget_mousepad_inertia"}) {
        document.clear();
        document[key] = 3;
        EXPECT_EQ(mousepad->validateConfig(document.as<JsonObject>()), nullptr);
        document[key] = "3";
        EXPECT_NE(mousepad->validateConfig(document.as<JsonObject>()), nullptr);
        document[key] = -1;
        EXPECT_NE(mousepad->validateConfig(document.as<JsonObject>()), nullptr);
        document[key] = 20;
        EXPECT_NE(mousepad->validateConfig(document.as<JsonObject>()), nullptr);
    }
    document.clear();
    document["widget_mousepad_reverse"] = "true";
    EXPECT_NE(mousepad->validateConfig(document.as<JsonObject>()), nullptr);
    document["widget_mousepad_reverse"] = true;
    document["widget_mousepad_inertia"] = 3;
    EXPECT_EQ(mousepad->validateConfig(document.as<JsonObject>()), nullptr);
    mousepad->parseConfig(document.as<JsonObject>(), config.data);
    const auto* parsed = reinterpret_cast<const MousepadConfig*>(config.data);
    EXPECT_TRUE(parsed->reverse);
    EXPECT_EQ(parsed->inertia, 3);
    EXPECT_EQ(parsed->sensitivity, 1);
    EXPECT_FALSE(parsed->button_zones_enabled);
    EXPECT_FALSE(parsed->show_back);
    document["widget_mousepad_back"] = "true";
    EXPECT_NE(mousepad->validateConfig(document.as<JsonObject>()), nullptr);
    document["widget_mousepad_back"] = true;
    EXPECT_EQ(mousepad->validateConfig(document.as<JsonObject>()), nullptr);
    mousepad->parseConfig(document.as<JsonObject>(), config.data);
    EXPECT_TRUE(parsed->show_back);
    document["widget_mousepad_buttons"] = "true";
    EXPECT_NE(mousepad->validateConfig(document.as<JsonObject>()), nullptr);
    document["widget_mousepad_buttons"] = true;
    EXPECT_EQ(mousepad->validateConfig(document.as<JsonObject>()), nullptr);
    mousepad->parseConfig(document.as<JsonObject>(), config.data);
    EXPECT_TRUE(parsed->button_zones_enabled);
    document["widget_mousepad_inertia"] = 20;
    mousepad->parseConfig(document.as<JsonObject>(), config.data);
    EXPECT_EQ(parsed->inertia, 5);
}

TEST(GamepadWidgetRegistry, SupportsFullFeaturedBoards) {
    static WidgetType placeholders[12] = {};
    for (auto& placeholder : placeholders) {
        placeholder.name = "capacity_probe";
        widget_register(&placeholder);
    }
    EXPECT_EQ(widget_count(), 16);
    EXPECT_EQ(widget_at(15), &placeholders[11]);
}