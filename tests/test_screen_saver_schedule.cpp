#include <assert.h>
#include <stdio.h>

#include "screen_saver_schedule.h"

static int g_tests = 0;
static int g_passed = 0;

#define TEST(name) static void name()
#define RUN(name) do { \
    g_tests++; \
    printf("  %-50s ", #name); \
    name(); \
    g_passed++; \
    printf("PASS\n"); \
} while (0)

#define ASSERT_ACTION(expected, ...) do { \
    ScreenSaverScheduleAction actual = screen_saver_schedule_action(__VA_ARGS__); \
    if (actual != (expected)) { \
        printf("FAIL\n    %s:%d\n", __FILE__, __LINE__); \
        assert(false); \
    } \
} while (0)

TEST(disabled_display_sleep_still_shows_idle_screen) {
    ASSERT_ACTION(ScreenSaverScheduleAction::ShowIdleScreen,
            false, 1800000, true, false, false, true, 300000, 300000);
}

TEST(disabled_display_sleep_without_idle_screen_stays_awake) {
    ASSERT_ACTION(ScreenSaverScheduleAction::None,
            false, 1800000, false, false, false, true, 300000, 1800000);
}

TEST(disabled_idle_screen_waits_for_sleep) {
    ASSERT_ACTION(ScreenSaverScheduleAction::None,
            true, 1800000, false, false, false, true, 300000, 300000);
    ASSERT_ACTION(ScreenSaverScheduleAction::Sleep,
            true, 1800000, false, false, false, true, 300000, 1800000);
}

TEST(idle_screen_waits_until_its_timeout) {
    ASSERT_ACTION(ScreenSaverScheduleAction::None,
            true, 1800000, true, false, false, true, 300000, 299999);
}

TEST(idle_screen_activates_at_its_timeout) {
    ASSERT_ACTION(ScreenSaverScheduleAction::ShowIdleScreen,
            true, 1800000, true, false, false, true, 300000, 300000);
}

TEST(idle_screen_does_not_repeat_while_active_or_attempted) {
    ASSERT_ACTION(ScreenSaverScheduleAction::None,
            true, 1800000, true, true, false, true, 300000, 600000);
    ASSERT_ACTION(ScreenSaverScheduleAction::None,
            true, 1800000, true, false, true, true, 300000, 600000);
}

TEST(missing_idle_pad_skips_to_display_sleep) {
    ASSERT_ACTION(ScreenSaverScheduleAction::None,
            true, 1800000, true, false, false, false, 300000, 300000);
    ASSERT_ACTION(ScreenSaverScheduleAction::Sleep,
            true, 1800000, true, false, false, false, 300000, 1800000);
}

TEST(display_sleep_wins_at_or_after_its_timeout) {
    ASSERT_ACTION(ScreenSaverScheduleAction::Sleep,
            true, 1800000, true, false, false, true, 1800000, 1800000);
    ASSERT_ACTION(ScreenSaverScheduleAction::Sleep,
            true, 1800000, true, true, false, true, 300000, 1800001);
}

int main() {
    printf("=== Screen saver schedule tests ===\n");
    RUN(disabled_display_sleep_still_shows_idle_screen);
    RUN(disabled_display_sleep_without_idle_screen_stays_awake);
    RUN(disabled_idle_screen_waits_for_sleep);
    RUN(idle_screen_waits_until_its_timeout);
    RUN(idle_screen_activates_at_its_timeout);
    RUN(idle_screen_does_not_repeat_while_active_or_attempted);
    RUN(missing_idle_pad_skips_to_display_sleep);
    RUN(display_sleep_wins_at_or_after_its_timeout);
    printf("\n%d/%d tests passed\n", g_passed, g_tests);
    return g_passed == g_tests ? 0 : 1;
}