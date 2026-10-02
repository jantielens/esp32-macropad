#include <gtest/gtest.h>
#include "mouse_hid_state.h"
#include "widgets/mousepad_input.h"
#include "widgets/scrollpad_input.h"

TEST(Mousepad, TapAndHold) {
    MousepadInput input;
    int dx, dy;
    input.press(20, 20, 100);
    input.move(22, 22, 1, dx, dy);
    EXPECT_EQ(dx, 0);
    EXPECT_TRUE(input.release(200));
    input.press(20, 20, 300);
    EXPECT_FALSE(input.release(600));
}

TEST(Mousepad, ThreePixelMovementThreshold) {
    MousepadInput input;
    int dx, dy;
    input.press(20, 20, 100);
    input.move(23, 20, 1, dx, dy);
    EXPECT_EQ(dx, 0);
    EXPECT_EQ(dy, 0);
    EXPECT_TRUE(input.release(200));
    input.press(20, 20, 300);
    input.move(24, 20, 1, dx, dy);
    EXPECT_EQ(dx, 4);
    EXPECT_EQ(dy, 0);
    input.move(25, 20, 1, dx, dy);
    EXPECT_EQ(dx, 1);
    EXPECT_FALSE(input.release(400));
    input.press(20, 20, 500);
    input.move(18, 17, 1, dx, dy);
    EXPECT_EQ(dx, -2);
    EXPECT_EQ(dy, -3);
    EXPECT_FALSE(input.release(600));
}

TEST(Mousepad, ConfigurableMovementThreshold) {
    MousepadInput input;
    int dx, dy;
    input.press(20, 20, 100);
    input.move(20, 20, 1, dx, dy, 110, 0, 0);
    EXPECT_EQ(dx, 0);
    EXPECT_TRUE(input.release(200));
    input.press(20, 20, 300);
    input.move(21, 20, 1, dx, dy, 310, 0, 0);
    EXPECT_EQ(dx, 1);
    EXPECT_FALSE(input.release(400));
    for (const float threshold : {3.0f, 6.0f, MousepadInput::max_movement_threshold}) {
        input.press(20, 20, 500);
        input.move(20 + static_cast<int>(threshold), 20, 1, dx, dy, 510, 5, threshold);
        EXPECT_EQ(dx, 0);
        EXPECT_EQ(dy, 0);
        EXPECT_TRUE(input.release(600));
        input.press(20, 20, 700);
        input.move(20 + static_cast<int>(threshold) + 1, 20, 1, dx, dy, 710, 0, threshold);
        EXPECT_EQ(dx, static_cast<int>(threshold) + 1);
        EXPECT_FALSE(input.release(800));
    }
}

TEST(Mousepad, MovementCancelsClickEvenAfterReturning) {
    MousepadInput input;
    int dx, dy;
    input.press(20, 20, 100);
    input.move(30, 25, 1, dx, dy);
    EXPECT_EQ(dx, 10);
    EXPECT_EQ(dy, 5);
    input.move(20, 20, 1, dx, dy);
    EXPECT_EQ(dx, -10);
    EXPECT_FALSE(input.release(200));
}

TEST(Mousepad, RepositionAndFractionalMovement) {
    MousepadInput input;
    int dx, dy;
    input.press(10, 10, 0);
    input.move(20, 10, 0.25f, dx, dy);
    EXPECT_EQ(dx, 2);
    input.move(21, 10, 0.25f, dx, dy);
    EXPECT_EQ(dx, 0);
    input.move(22, 10, 0.25f, dx, dy);
    EXPECT_EQ(dx, 1);
    input.release(50);
    input.press(200, 200, 100);
    input.move(208, 192, 1, dx, dy);
    EXPECT_EQ(dx, 8);
    EXPECT_EQ(dy, -8);
    input.cancel();
    input.move(300, 300, 1, dx, dy);
    EXPECT_EQ(dx, 0);
    EXPECT_FALSE(input.release(150));
}

TEST(MouseReports, SplitAndRetryMovement) {
    MouseHidState state;
    MouseHidReport report;
    ASSERT_TRUE(state.next(report));
    state.acknowledge(report);
    state.move(300, -200);
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.dx, 127);
    EXPECT_EQ(report.dy, -127);
    MouseHidReport retry;
    ASSERT_TRUE(state.next(retry));
    EXPECT_EQ(retry.dx, report.dx);
    state.move(10, 0);
    state.acknowledge(report);
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.dx, 127);
    EXPECT_EQ(report.dy, -73);
    state.acknowledge(report);
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.dx, 56);
    EXPECT_EQ(report.dy, 0);
    state.acknowledge(report);
    EXPECT_FALSE(state.next(report));
}

TEST(MouseReports, ClickReleaseAndReset) {
    MouseHidState state;
    MouseHidReport report;
    ASSERT_TRUE(state.next(report));
    state.acknowledge(report);
    ASSERT_TRUE(state.click());
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.buttons, 1);
    state.acknowledge(report);
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.buttons, 0);
    state.reset();
    state.acknowledge(report);
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.buttons, 0);
    state.acknowledge(report);
    EXPECT_FALSE(state.next(report));
}

TEST(MouseReports, MovementPrecedesClickAndReleasePrecedesNewMovement) {
    MouseHidState state;
    MouseHidReport report;
    state.next(report);
    state.acknowledge(report);
    state.move(20, 10);
    state.click();
    state.next(report);
    EXPECT_EQ(report.dx, 20);
    EXPECT_EQ(report.buttons, 0);
    state.acknowledge(report);
    state.next(report);
    EXPECT_EQ(report.buttons, 1);
    state.acknowledge(report);
    state.move(5, 0);
    state.next(report);
    EXPECT_EQ(report.buttons, 0);
    EXPECT_EQ(report.dx, 0);
    state.acknowledge(report);
    state.next(report);
    EXPECT_EQ(report.dx, 5);
}

TEST(MouseReports, MixedButtonsRetryReleaseAndWraparound) {
    MouseHidState state;
    MouseHidReport report;
    state.next(report);
    state.acknowledge(report);
    for (int index = 0; index < 16; ++index) {
        const uint8_t buttons = static_cast<uint8_t>(1 << (index % 3));
        ASSERT_TRUE(state.click(buttons));
        ASSERT_TRUE(state.next(report));
        EXPECT_EQ(report.buttons, buttons);
        MouseHidReport retry;
        ASSERT_TRUE(state.next(retry));
        EXPECT_EQ(retry.buttons, buttons);
        state.acknowledge(report);
        ASSERT_TRUE(state.next(report));
        EXPECT_EQ(report.buttons, 0);
        state.acknowledge(report);
        EXPECT_FALSE(state.next(report));
    }
    ASSERT_TRUE(state.click(2));
    ASSERT_TRUE(state.click(4));
    ASSERT_TRUE(state.click(1));
    for (const uint8_t buttons : {2, 4, 1}) {
        ASSERT_TRUE(state.next(report));
        EXPECT_EQ(report.buttons, buttons);
        state.acknowledge(report);
        ASSERT_TRUE(state.next(report));
        EXPECT_EQ(report.buttons, 0);
        state.acknowledge(report);
    }
    EXPECT_FALSE(state.next(report));
}

TEST(Scrollpad, AxesDirectionAndSensitivity) {
    ScrollpadInput input;
    EXPECT_EQ(input.move(100, 100, false, 1, false), 0);
    input.press(100, 100);
    EXPECT_EQ(input.move(200, 100, false, 1, false), 0);
    EXPECT_EQ(input.move(200, 80, false, 1, false), 1);
    EXPECT_EQ(input.move(200, 100, false, 1, false), -1);
    EXPECT_EQ(input.move(200, 80, false, 1, true), -1);
    input.press(100, 100);
    EXPECT_EQ(input.move(100, 200, true, 1, false), 0);
    EXPECT_EQ(input.move(120, 200, true, 1, false), 1);
    EXPECT_EQ(input.move(100, 200, true, 1, false), -1);
    EXPECT_EQ(input.move(120, 200, true, 1, true), -1);
    input.press(0, 0);
    EXPECT_EQ(input.move(10, 0, true, 2, false), 1);
    EXPECT_EQ(input.move(110, 0, true, 0.1f, false), 0);
    EXPECT_EQ(input.move(210, 0, true, 0.1f, false), 1);
}

TEST(Scrollpad, FractionsRepositionAndCancel) {
    ScrollpadInput input;
    input.press(0, 100);
    for (int position = 99; position > 80; --position) {
        EXPECT_EQ(input.move(0, position, false, 1, false), 0);
    }
    EXPECT_EQ(input.move(0, 80, false, 1, false), 1);
    EXPECT_EQ(input.move(0, 90, false, 1, false), 0);
    input.cancel();
    EXPECT_EQ(input.move(0, 200, false, 1, false), 0);
    input.press(0, 200);
    EXPECT_EQ(input.move(0, 190, false, 1, false), 0);
    EXPECT_EQ(input.move(0, 180, false, 1, false), 1);
}

TEST(MouseReports, ScrollSplitRetryReleaseAndReset) {
    MouseHidState state;
    MouseHidReport report;
    ASSERT_TRUE(state.next(report));
    state.acknowledge(report);
    state.scroll(200, -180);
    state.move(10, -5);
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.wheel, 127);
    EXPECT_EQ(report.pan, -127);
    EXPECT_EQ(report.dx, 10);
    EXPECT_EQ(report.dy, -5);
    MouseHidReport retry;
    ASSERT_TRUE(state.next(retry));
    EXPECT_EQ(retry.wheel, report.wheel);
    EXPECT_EQ(retry.pan, report.pan);
    state.acknowledge(report);
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.wheel, 73);
    EXPECT_EQ(report.pan, -53);
    state.acknowledge(report);
    ASSERT_TRUE(state.click(2));
    state.next(report);
    state.acknowledge(report);
    state.scroll(1, 2);
    ASSERT_TRUE(state.next(report));
    EXPECT_EQ(report.buttons, 0);
    EXPECT_EQ(report.wheel, 0);
    EXPECT_EQ(report.pan, 0);
    state.acknowledge(report);
    state.next(report);
    state.reset();
    state.acknowledge(report);
    state.next(report);
    EXPECT_EQ(report.wheel, 0);
    EXPECT_EQ(report.pan, 0);
    state.acknowledge(report);
    EXPECT_FALSE(state.next(report));
}

TEST(Mousepad, AccelerationPreservesSlowMovementAndTap) {
    MousepadInput input;
    int dx, dy;
    input.press(0, 0, 0);
    input.move(10, 0, 1, dx, dy, 100, 5);
    EXPECT_EQ(dx, 10);
    EXPECT_EQ(dy, 0);
    input.press(0, 0, 0);
    input.move(10, 0, 1, dx, dy, 10, 0);
    EXPECT_EQ(dx, 10);
    input.press(0, 0, 0);
    input.move(10, 0, 1, dx, dy, 10, 1);
    const int moderate = dx;
    EXPECT_GT(moderate, 10);
    input.press(0, 0, 0);
    input.move(10, 0, 1, dx, dy, 10, 5);
    EXPECT_GT(dx, moderate);
    EXPECT_LE(dx, 35);
    input.move(20, 0, 1, dx, dy, 10, 5);
    EXPECT_LE(dx, 11);
    input.press(0, 0, 0);
    input.move(2, 2, 1, dx, dy, 10, 5);
    EXPECT_EQ(dx, 0);
    EXPECT_TRUE(input.release(100));
}

TEST(Scrollpad, ReleaseVelocityPausesAndDirection) {
    ScrollpadInput input;
    input.press(0, 100, 0);
    input.move(0, 90, false, 1, false, 20);
    EXPECT_GT(input.release(20), 2);
    EXPECT_EQ(input.release(30), 0);
    input.press(0, 100, 0);
    input.move(0, 90, false, 1, false, 20);
    EXPECT_EQ(input.release(120), 0);
    input.press(0, 0, 0);
    input.move(20, 0, true, 1, true, 20);
    EXPECT_LT(input.release(20), -2);
    input.press(0, 0, 0);
    input.move(0, 20, true, 1, false, 20);
    EXPECT_EQ(input.release(20), 0);
}

TEST(MouseReports, InertiaStrengthAxesAndSafety) {
    auto coast_steps = [](float strength, bool horizontal) {
        MouseHidState state;
        MouseHidReport report;
        state.next(report);
        state.acknowledge(report);
        state.start_inertia(20, strength, horizontal, 0);
        int total = 0;
        for (uint32_t now = 10; now <= 4000; now += 10) {
            state.tick(now);
            if (state.next(report)) {
                total += horizontal ? report.pan : report.wheel;
                EXPECT_EQ(horizontal ? report.wheel : report.pan, 0);
                EXPECT_EQ(report.buttons, 0);
                EXPECT_EQ(report.dx, 0);
                state.acknowledge(report);
            }
        }
        EXPECT_FALSE(state.next(report));
        return total;
    };
    EXPECT_EQ(coast_steps(0, false), 0);
    EXPECT_GT(coast_steps(1, false), 0);
    EXPECT_GT(coast_steps(5, false), coast_steps(1, false));
    EXPECT_EQ(coast_steps(5, true), coast_steps(5, false));
    MouseHidState state;
    MouseHidReport report;
    state.next(report);
    state.acknowledge(report);
    state.start_inertia(10000, 5, true, 0);
    state.tick(100);
    ASSERT_TRUE(state.next(report));
    EXPECT_LE(report.pan, 4);
    state.acknowledge(report);
    state.tick(250);
    EXPECT_FALSE(state.next(report));
    state.start_inertia(1, 5, false, 250);
    state.tick(300);
    EXPECT_FALSE(state.next(report));
}

TEST(MouseReports, StopCoastPreservesClicksAndRejectsStaleScrollAcknowledgement) {
    MouseHidState state;
    MouseHidReport report;
    state.next(report);
    state.acknowledge(report);
    state.scroll(5, 0);
    state.move(3, 0);
    state.click(2);
    state.next(report);
    state.stop_scroll();
    state.scroll(2, 0);
    state.acknowledge(report);
    state.next(report);
    EXPECT_EQ(report.dx, 0);
    EXPECT_EQ(report.wheel, 2);
    state.acknowledge(report);
    state.next(report);
    EXPECT_EQ(report.buttons, 2);
    state.acknowledge(report);
    state.next(report);
    state.acknowledge(report);
    state.start_inertia(-20, 5, false, 0);
    state.tick(100);
    ASSERT_TRUE(state.next(report));
    EXPECT_LT(report.wheel, 0);
    state.stop_scroll();
    state.tick(110);
    EXPECT_FALSE(state.next(report));
    state.start_inertia(20, 5, false, 110);
    state.reset();
    state.tick(120);
    state.next(report);
    EXPECT_EQ(report.wheel, 0);
    state.acknowledge(report);
    EXPECT_FALSE(state.next(report));
}

TEST(MouseReports, SubmissionRejectsStaleEpochAndOta) {
    MouseHidReport report;
    report.dx = 1;
    EXPECT_TRUE(report.can_submit(1, 1, 2, 2, false));
    EXPECT_FALSE(report.can_submit(1, 2, 2, 2, false));
    EXPECT_FALSE(report.can_submit(1, 1, 2, 2, true));
    EXPECT_FALSE(report.can_submit(1, 1, 2, 3, false));
    report.dx = 0;
    report.buttons = 1;
    EXPECT_FALSE(report.can_submit(1, 1, 2, 2, true));
    report.buttons = 0;
    report.wheel = 1;
    EXPECT_FALSE(report.can_submit(1, 1, 2, 3, false));
    report.wheel = 0;
    EXPECT_TRUE(report.can_submit(1, 1, 2, 3, true));
    EXPECT_FALSE(report.can_submit(1, 2, 2, 3, true));
}

TEST(MouseReports, InertiaSnapshotRejectsCancellationAndPreservesNewInput) {
    MouseHidState state;
    MouseHidReport report;
    state.next(report);
    state.acknowledge(report);
    state.start_inertia(20, 5, false, 0);
    auto tick = state.inertia_tick(100);
    state.move(3, 0);
    state.scroll(5, 0);
    state.click(2);
    state.apply_inertia(tick);
    state.next(report);
    EXPECT_EQ(report.dx, 3);
    EXPECT_GT(report.wheel, 5);
    state.acknowledge(report);
    tick = state.inertia_tick(200);
    state.stop_scroll();
    state.apply_inertia(tick);
    state.next(report);
    EXPECT_EQ(report.wheel, 0);
    EXPECT_EQ(report.buttons, 2);
    state.start_inertia(20, 5, false, 200);
    tick = state.inertia_tick(300);
    state.start_inertia(-20, 5, true, 200);
    state.apply_inertia(tick);
    state.next(report);
    EXPECT_EQ(report.wheel, 0);
    EXPECT_EQ(report.pan, 0);
    tick = state.inertia_tick(300);
    state.apply_inertia(tick);
    state.next(report);
    EXPECT_LT(report.pan, 0);
    state.acknowledge(report);
    state.apply_inertia(tick);
    state.next(report);
    EXPECT_EQ(report.pan, 0);
    tick = state.inertia_tick(400);
    state.reset();
    state.apply_inertia(tick);
    state.next(report);
    EXPECT_EQ(report.pan, 0);
    EXPECT_EQ(report.wheel, 0);
}

TEST(MouseReports, RejectInvalidButtons) {
    MouseHidState state;
    EXPECT_FALSE(state.click(0));
    EXPECT_FALSE(state.click(3));
    EXPECT_FALSE(state.click(8));
    EXPECT_FALSE(state.click(255));
}

TEST(MouseReports, ResetDropsStaleMovementAndClicks) {
    MouseHidState state;
    MouseHidReport report;
    state.next(report);
    state.acknowledge(report);
    state.move(50, 25);
    state.next(report);
    state.reset();
    state.move(10, 5);
    state.acknowledge(report);
    state.next(report);
    EXPECT_EQ(report.dx, 0);
    state.acknowledge(report);
    state.next(report);
    EXPECT_EQ(report.dx, 10);
    EXPECT_EQ(report.dy, 5);
    state.acknowledge(report);
    for (int index = 0; index < 8; ++index) EXPECT_TRUE(state.click());
    EXPECT_FALSE(state.click());
    state.reset();
    state.next(report);
    state.acknowledge(report);
    EXPECT_FALSE(state.next(report));
}