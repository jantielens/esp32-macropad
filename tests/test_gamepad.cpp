#include <gtest/gtest.h>
#include "gamepad_hid_state.h"
#include "widgets/gamepad_joystick_input.h"
#include "touch_sample.h"

namespace {
const GamepadControl button{GamepadControlKind::Button, 0};
const GamepadControl up{GamepadControlKind::Hat, 0};
const GamepadControl down{GamepadControlKind::Hat, 1};
const GamepadControl right{GamepadControlKind::Hat, 3};

void drain(GamepadHidState& state, uint32_t now = 0) {
    GamepadReport report;
    unsigned reports = 0;
    while (state.next(report)) {
        ASSERT_LT(++reports, 256U);
        state.acknowledge(report, now);
    }
}
}

TEST(GamepadState, NeutralAndRetry) {
    GamepadHidState state;
    GamepadReport report, retry;
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.buttons, 0);
    EXPECT_EQ(report.hat, 0);
    ASSERT_TRUE(state.next(retry));
    EXPECT_EQ(report.sequence, retry.sequence);
    state.acknowledge(report, 0);
    EXPECT_FALSE(state.next(report));
}

TEST(GamepadState, OwnershipAndIdempotentStandaloneHolds) {
    GamepadHidState state;
    drain(state);
    const uint32_t owner = state.acquire(button);
    ASSERT_NE(owner, 0U);
    ASSERT_TRUE(state.hold(button, true));
    ASSERT_TRUE(state.hold(button, true));
    ASSERT_TRUE(state.hold(button, false));
    GamepadReport report;
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.buttons, 1);
    state.acknowledge(report, 0);
    EXPECT_FALSE(state.next(report));
    ASSERT_TRUE(state.release(owner));
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.buttons, 0);
}

TEST(GamepadState, ShortInteractionPreservesBothEdges) {
    GamepadHidState state;
    drain(state);
    const auto owner = state.acquire(button);
    ASSERT_TRUE(state.release(owner));
    GamepadReport report;
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.buttons, 1);
    state.acknowledge(report, 100);
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.buttons, 0);
}

TEST(GamepadState, HatAndIndependentTriggers) {
    GamepadHidState state;
    ASSERT_TRUE(state.hold(up, true));
    ASSERT_TRUE(state.hold(right, true));
    drain(state);
    const auto stick = state.acquire_stick(0);
    ASSERT_TRUE(state.move_stick(stick, 1, 2));
    GamepadReport report;
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.hat, 2);
    state.acknowledge(report, 0);
    ASSERT_TRUE(state.hold(down, true));
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.hat, 3);
    drain(state);
    ASSERT_TRUE(state.hold({GamepadControlKind::Trigger, 0}, true));
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.left_trigger, 255);
    EXPECT_EQ(report.right_trigger, 0);
}

TEST(GamepadState, StickCaptureAndCoalescing) {
    GamepadHidState state;
    drain(state);
    const auto left = state.acquire_stick(0);
    const auto other = state.acquire_stick(1);
    ASSERT_NE(left, 0U);
    EXPECT_EQ(state.acquire_stick(0), 0U);
    state.move_stick(left, 200, -300);
    state.move_stick(left, 32767, -32767);
    state.move_stick(other, -123, 456);
    GamepadReport report;
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.left_x, 32767);
    EXPECT_EQ(report.right_x, -123);
    state.move_stick(left, 0, 20);
    state.acknowledge(report, 0);
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.left_y, 20);
    EXPECT_EQ(report.right_y, 456);
    state.release(left);
    ASSERT_NE(state.acquire_stick(0), 0U);
}

TEST(GamepadState, TapStartsClockOnlyAfterPressAndCompletesAfterRelease) {
    GamepadHidState state;
    drain(state);
    ASSERT_TRUE(state.tap(button, 42));
    EXPECT_FALSE(state.tap(button, 43));
    EXPECT_EQ(state.acquire(button), 0U);
    state.tick(10000);
    GamepadReport report;
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.buttons, 1);
    state.acknowledge(report, 10000);
    state.tick(10049);
    EXPECT_FALSE(state.next(report));
    state.tick(10050);
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.buttons, 0);
    uint32_t token = 0;
    bool success = false;
    EXPECT_FALSE(state.take_completion(token, success));
    state.acknowledge(report, 10050);
    ASSERT_TRUE(state.take_completion(token, success));
    EXPECT_EQ(token, 42U);
    EXPECT_TRUE(success);
}

TEST(GamepadState, ResetInvalidatesOwnersReportsAndTaps) {
    GamepadHidState state;
    drain(state);
    const auto owner = state.acquire_stick(0);
    state.move_stick(owner, 200, 300);
    ASSERT_TRUE(state.tap(button, 42));
    GamepadReport old_report, report;
    state.next(old_report);
    state.reset();
    state.acknowledge(old_report, 0);
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.left_x, 0);
    EXPECT_EQ(report.buttons, 0);
    EXPECT_FALSE(state.move_stick(owner, 1, 2));
    uint32_t token;
    bool success;
    ASSERT_TRUE(state.take_completion(token, success));
    EXPECT_EQ(token, 42U);
    EXPECT_FALSE(success);
}

TEST(GamepadState, CapacityReservesEveryAcceptedRelease) {
    GamepadHidState state;
    unsigned accepted = 0;
    while (state.hold(button, true)) {
        ASSERT_TRUE(state.hold(button, false));
        ASSERT_LT(++accepted, 128U);
    }
    EXPECT_GT(accepted, 0U);
    GamepadReport report;
    drain(state);
    EXPECT_FALSE(state.next(report));
    ASSERT_TRUE(state.hold(button, true));
    ASSERT_TRUE(state.hold(button, false));
}

TEST(GamepadState, ResetFailsUnconsumedTapCompletion) {
    GamepadHidState state;
    drain(state);
    ASSERT_TRUE(state.tap(button, 42));
    drain(state, 100);
    state.tick(150);
    drain(state, 150);
    state.reset();
    uint32_t token;
    bool success;
    ASSERT_TRUE(state.take_completion(token, success));
    EXPECT_FALSE(success);
}

TEST(GamepadProtocol, ControlMasksFitReportAndDoNotOverlap) {
    uint32_t button_mask = 0;
    for (uint8_t index = 0; index < gamepad_protocol::button_count; ++index) {
        const uint32_t mask = GamepadControl{GamepadControlKind::Button, index}.mask();
        EXPECT_NE(mask, 0U);
        EXPECT_EQ(button_mask & mask, 0U);
        button_mask |= mask;
    }
    EXPECT_EQ(button_mask, UINT16_MAX);
    uint32_t control_mask = button_mask;
    for (const auto kind : {GamepadControlKind::Hat, GamepadControlKind::Trigger}) {
        const uint8_t count = kind == GamepadControlKind::Hat ?
            gamepad_protocol::hat_control_count : gamepad_protocol::trigger_count;
        for (uint8_t index = 0; index < count; ++index) {
            const uint32_t mask = GamepadControl{kind, index}.mask();
            EXPECT_NE(mask, 0U);
            EXPECT_EQ(control_mask & mask, 0U);
            control_mask |= mask;
        }
        EXPECT_EQ((GamepadControl{kind, count}.mask()), 0U);
    }
    EXPECT_EQ((GamepadControl{GamepadControlKind::Button, gamepad_protocol::button_count}.mask()), 0U);
    EXPECT_EQ(gamepad_protocol::report_bytes, 13U);
}

TEST(GamepadJoystick, GeometryDeadZoneAndRadialClamping) {
    GamepadJoystickInput input;
    EXPECT_FALSE(input.press(0, 0, 0, 0, 5, 100, false));
    ASSERT_TRUE(input.press(50, 50, 0, 0, 100, 200, false));
    EXPECT_EQ(input.base_radius, 50.0f);
    EXPECT_EQ(input.thumb_radius, 16.5f);
    EXPECT_EQ(input.radius, 33.5f);
    EXPECT_EQ(input.move(50, 100, .1f, false, false).horizontal, 0);
    EXPECT_EQ(input.move(53, 100, .1f, false, false).horizontal, 0);
    EXPECT_EQ(input.move(83.5f, 100, .1f, false, false).horizontal, 32767);
    const auto diagonal = input.move(500, 550, .1f, false, false);
    EXPECT_NEAR(diagonal.horizontal, 23170, 1);
    EXPECT_NEAR(diagonal.vertical, 23170, 1);
    EXPECT_EQ(input.move(83.5f, 100, .1f, true, false).horizontal, -32767);
    input.cancel();
    EXPECT_EQ(input.move(500, 550, .1f, false, false).horizontal, 0);
}

TEST(GamepadJoystick, FloatingBaseStaysInside) {
    GamepadJoystickInput input;
    ASSERT_TRUE(input.press(0, 300, 10, 20, 100, 200, true));
    EXPECT_EQ(input.center_x, 60.0f);
    EXPECT_EQ(input.center_y, 170.0f);
    input.move(500, 500, .1f, false, false);
    EXPECT_LE(input.thumb_x + input.thumb_radius, input.center_x + input.base_radius);
    EXPECT_LE(input.thumb_y + input.thumb_radius, input.center_y + input.base_radius);
}

TEST(GamepadTouch, NoNewScanIsNotRelease) {
    TouchSampleFilter filter;
    TouchSample sample;
    sample.pressed = true;
    sample.horizontal = 20;
    EXPECT_TRUE(filter.update(sample, 0).pressed);
    sample.status = TouchReadStatus::Unchanged;
    EXPECT_TRUE(filter.update(sample, 10000).pressed);
    EXPECT_FALSE(filter.canceled);
}

TEST(GamepadTouch, ErrorsCancelAndRequirePhysicalRelease) {
    TouchSampleFilter filter;
    TouchSample sample;
    sample.pressed = true;
    filter.update(sample, 0);
    sample.status = TouchReadStatus::Error;
    EXPECT_TRUE(filter.update(sample, 10).pressed);
    EXPECT_TRUE(filter.update(sample, 109).pressed);
    EXPECT_FALSE(filter.update(sample, 110).pressed);
    EXPECT_TRUE(filter.canceled);
    sample.status = TouchReadStatus::Fresh;
    EXPECT_FALSE(filter.update(sample, 120).pressed);
    sample.pressed = false;
    EXPECT_FALSE(filter.update(sample, 130).pressed);
    sample.pressed = true;
    EXPECT_TRUE(filter.update(sample, 140).pressed);
}

TEST(GamepadTouch, FreshScanRecoversBeforeTimeout) {
    TouchSampleFilter filter;
    TouchSample sample;
    sample.pressed = true;
    filter.update(sample, 0);
    sample.status = TouchReadStatus::Error;
    filter.update(sample, 10);
    sample.status = TouchReadStatus::Fresh;
    EXPECT_TRUE(filter.update(sample, 50).pressed);
    sample.status = TouchReadStatus::Unchanged;
    EXPECT_TRUE(filter.update(sample, 200).pressed);
}