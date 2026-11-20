// ============================================================================
// Unit tests for raw timer values and edge cases.
// ============================================================================
// Host-native: compiled with a controllable millis() mock.

#include <cassert>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Controllable millis() mock
// ---------------------------------------------------------------------------
static unsigned long s_millis = 0;
extern "C" unsigned long millis() { return s_millis; }

// ---------------------------------------------------------------------------
// Stub: action_dispatch (referenced by timer_engine_tick, not under test)
// ---------------------------------------------------------------------------
#include "pad_config.h"
#include "action_continuation.h"
#include "action_result.h"
ActionResult action_dispatch(const ButtonAction&, const char*, uint32_t, bool) {
    return ACTION_COMPLETE;
}
void action_list_dispatch(const ButtonAction*, uint8_t, const char*,
                          ActionContinuationOwner, bool) {}

// ---------------------------------------------------------------------------
// Unit under test
// ---------------------------------------------------------------------------
#include "timer_engine.h"
#include "timer_engine.cpp"  // include .cpp for access to internal state

// ---------------------------------------------------------------------------
// Minimal test harness (matches project pattern)
// ---------------------------------------------------------------------------
static int g_tests = 0;
static int g_passed = 0;

#define TEST(name) static void name()
#define RUN(name) do {                                       \
    g_tests++;                                               \
    printf("  %-50s ", #name);                               \
    name();                                                  \
    g_passed++;                                              \
    printf("PASS\n");                                        \
} while (0)

#define ASSERT_STR_EQ(actual, expected) do {                 \
    if (strcmp((actual), (expected)) != 0) {                  \
        printf("FAIL\n    %s:%d: \"%s\" != \"%s\"\n",       \
               __FILE__, __LINE__, (actual), (expected));    \
        assert(false);                                       \
    }                                                        \
} while (0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static char buf[64];

// Set up a count-up timer at a specific elapsed time
static void setup_countup(uint8_t id, uint32_t elapsed_ms) {
    timer_engine_init();
    s_millis = 0;
    timer_configure_and_start(id, TIMER_MODE_UP, 0, nullptr, 0);
    s_millis = elapsed_ms;
}

// Set up a countdown timer with a preset, running for a given elapsed time
static void setup_countdown(uint8_t id, uint32_t preset_s, uint32_t elapsed_ms) {
    timer_engine_init();
    s_millis = 0;
    timer_configure_and_start(id, TIMER_MODE_DOWN, preset_s * 1000, nullptr, 0);
    s_millis = elapsed_ms;
}

// ===========================================================================
// Default format (NULL) — raw seconds with decisecond
// ===========================================================================
TEST(default_zero) {
    setup_countup(1, 0);
    timer_format(1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0.0");
}

TEST(default_sub_second) {
    setup_countup(1, 300);  // 0.3s
    timer_format(1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0.3");
}

TEST(default_45_3s) {
    setup_countup(1, 45300);
    timer_format(1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "45.3");
}

TEST(default_300s) {
    setup_countup(1, 300000);
    timer_format(1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "300.0");
}

TEST(default_large_value) {
    setup_countup(1, 3661200);  // 1h 1m 1.2s
    timer_format(1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "3661.2");
}

TEST(default_decisecond_truncation) {
    setup_countup(1, 999);  // 999ms → 0s, ds=9
    timer_format(1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0.9");
}

// ===========================================================================
// Countdown — remaining time (not yet expired)
// ===========================================================================
TEST(countdown_remaining_default) {
    setup_countdown(1, 60, 15000);  // 60s preset, 15s elapsed → 45s remaining
    timer_format(1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "45.0");
}

// ===========================================================================
// Countdown — overtime (past zero, negative sign)
// ===========================================================================
TEST(overtime_default) {
    setup_countdown(1, 10, 15300);  // 10s preset, 15.3s elapsed → -5.3s
    timer_format(1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "-5.3");
}

// ===========================================================================
// Edge: stopped timer = 0
// ===========================================================================
TEST(stopped_timer_zero) {
    timer_engine_init();
    timer_format(1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0.0");
}

// ===========================================================================
// Edge: all three timer IDs work
// ===========================================================================
TEST(all_timer_ids) {
    timer_engine_init();
    s_millis = 0;
    for (uint8_t id = 1; id <= 3; id++) {
        timer_configure_and_start(id, TIMER_MODE_UP, 0, nullptr, 0);
    }
    s_millis = 1000 * 1;  // 1s
    timer_format(1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1.0");
    timer_format(2, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1.0");
    timer_format(3, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1.0");
}

// ===========================================================================
// Edge: buffer too small
// ===========================================================================
TEST(small_buffer) {
    setup_countup(1, 45300);
    char tiny[4];
    timer_format(1, tiny, sizeof(tiny));
    ASSERT_STR_EQ(tiny, "45.");  // truncated by snprintf
}

// ===========================================================================
int main() {
    printf("\n--- timer_format: default (numeric) ---\n");
    RUN(default_zero);
    RUN(default_sub_second);
    RUN(default_45_3s);
    RUN(default_300s);
    RUN(default_large_value);
    RUN(default_decisecond_truncation);

    printf("\n--- timer_format: countdown remaining ---\n");
    RUN(countdown_remaining_default);

    printf("\n--- timer_format: countdown overtime ---\n");
    RUN(overtime_default);

    printf("\n--- timer_format: edge cases ---\n");
    RUN(stopped_timer_zero);
    RUN(all_timer_ids);
    RUN(small_buffer);

    printf("\n%d passed, %d failed\n", g_passed, g_tests - g_passed);
    return g_tests - g_passed;
}
