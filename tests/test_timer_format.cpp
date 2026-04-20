// ============================================================================
// Unit tests for timer_format() — all format branches and edge cases
// ============================================================================
// Host-native: compiled with a controllable millis() mock.

#include <cassert>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Controllable millis() mock
// ---------------------------------------------------------------------------
uint32_t g_mock_millis = 0;

// ---------------------------------------------------------------------------
// Stub: action_dispatch (referenced by timer_engine_tick, not under test)
// ---------------------------------------------------------------------------
#include "pad_config.h"
void action_dispatch(const ButtonAction&, const char*) {}

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
    g_mock_millis = 0;
    timer_set_mode(id, TIMER_MODE_UP);
    timer_start(id);
    g_mock_millis = elapsed_ms;
}

// Set up a countdown timer with a preset, running for a given elapsed time
static void setup_countdown(uint8_t id, uint32_t preset_s, uint32_t elapsed_ms) {
    timer_engine_init();
    g_mock_millis = 0;
    timer_set_mode(id, TIMER_MODE_DOWN);
    timer_set_countdown(id, preset_s);
    timer_start(id);
    g_mock_millis = elapsed_ms;
}

// ===========================================================================
// Default format (NULL) — raw seconds with decisecond
// ===========================================================================
TEST(default_zero) {
    setup_countup(1, 0);
    timer_format(1, nullptr, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0.0");
}

TEST(default_sub_second) {
    setup_countup(1, 300);  // 0.3s
    timer_format(1, nullptr, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0.3");
}

TEST(default_45_3s) {
    setup_countup(1, 45300);
    timer_format(1, nullptr, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "45.3");
}

TEST(default_300s) {
    setup_countup(1, 300000);
    timer_format(1, nullptr, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "300.0");
}

TEST(default_large_value) {
    setup_countup(1, 3661200);  // 1h 1m 1.2s
    timer_format(1, nullptr, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "3661.2");
}

TEST(default_empty_string_format) {
    setup_countup(1, 45300);
    timer_format(1, "", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "45.3");
}

TEST(default_decisecond_truncation) {
    setup_countup(1, 999);  // 999ms → 0s, ds=9
    timer_format(1, nullptr, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0.9");
}

// ===========================================================================
// Named format: mm:ss
// ===========================================================================
TEST(mmss_zero) {
    setup_countup(1, 0);
    timer_format(1, "mm:ss", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0:00");
}

TEST(mmss_90s) {
    setup_countup(1, 90000);
    timer_format(1, "mm:ss", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1:30");
}

TEST(mmss_3661s) {
    setup_countup(1, 3661000);  // 1h 1m 1s
    timer_format(1, "mm:ss", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "61:01");
}

// ===========================================================================
// Named format: hh:mm:ss
// ===========================================================================
TEST(hhmmss_zero) {
    setup_countup(1, 0);
    timer_format(1, "hh:mm:ss", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0:00:00");
}

TEST(hhmmss_3661s) {
    setup_countup(1, 3661000);
    timer_format(1, "hh:mm:ss", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1:01:01");
}

// ===========================================================================
// Named format: ss
// ===========================================================================
TEST(ss_zero) {
    setup_countup(1, 0);
    timer_format(1, "ss", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0");
}

TEST(ss_45s) {
    setup_countup(1, 45300);
    timer_format(1, "ss", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "45");
}

TEST(ss_large) {
    setup_countup(1, 3661000);
    timer_format(1, "ss", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "3661");
}

// ===========================================================================
// Named format: mm:ss.d
// ===========================================================================
TEST(mmssd_zero) {
    setup_countup(1, 0);
    timer_format(1, "mm:ss.d", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0:00.0");
}

TEST(mmssd_90_7s) {
    setup_countup(1, 90700);
    timer_format(1, "mm:ss.d", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1:30.7");
}

// ===========================================================================
// Countdown — remaining time (not yet expired)
// ===========================================================================
TEST(countdown_remaining_default) {
    setup_countdown(1, 60, 15000);  // 60s preset, 15s elapsed → 45s remaining
    timer_format(1, nullptr, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "45.0");
}

TEST(countdown_remaining_mmss) {
    setup_countdown(1, 60, 15000);
    timer_format(1, "mm:ss", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0:45");
}

// ===========================================================================
// Countdown — overtime (past zero, negative sign)
// ===========================================================================
TEST(overtime_default) {
    setup_countdown(1, 10, 15300);  // 10s preset, 15.3s elapsed → -5.3s
    timer_format(1, nullptr, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "-5.3");
}

TEST(overtime_mmss) {
    setup_countdown(1, 10, 75000);  // 10s preset, 75s elapsed → -65s → -1:05
    timer_format(1, "mm:ss", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "-1:05");
}

TEST(overtime_ss) {
    setup_countdown(1, 10, 15000);  // -5s
    timer_format(1, "ss", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "-5");
}

TEST(overtime_mmssd) {
    setup_countdown(1, 10, 15300);  // -5.3s
    timer_format(1, "mm:ss.d", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "-0:05.3");
}

// ===========================================================================
// Unknown format — falls back to mm:ss
// ===========================================================================
TEST(unknown_format_fallback) {
    setup_countup(1, 90000);
    timer_format(1, "bogus", buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1:30");
}

// ===========================================================================
// Edge: stopped timer = 0
// ===========================================================================
TEST(stopped_timer_zero) {
    timer_engine_init();
    timer_format(1, nullptr, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0.0");
}

// ===========================================================================
// Edge: all three timer IDs work
// ===========================================================================
TEST(all_timer_ids) {
    timer_engine_init();
    g_mock_millis = 0;
    for (uint8_t id = 1; id <= 3; id++) {
        timer_set_mode(id, TIMER_MODE_UP);
        timer_start(id);
    }
    g_mock_millis = 1000 * 1;  // 1s
    timer_format(1, nullptr, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1.0");
    timer_format(2, nullptr, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1.0");
    timer_format(3, nullptr, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1.0");
}

// ===========================================================================
// Edge: buffer too small
// ===========================================================================
TEST(small_buffer) {
    setup_countup(1, 45300);
    char tiny[4];
    timer_format(1, nullptr, tiny, sizeof(tiny));
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
    RUN(default_empty_string_format);
    RUN(default_decisecond_truncation);

    printf("\n--- timer_format: mm:ss ---\n");
    RUN(mmss_zero);
    RUN(mmss_90s);
    RUN(mmss_3661s);

    printf("\n--- timer_format: hh:mm:ss ---\n");
    RUN(hhmmss_zero);
    RUN(hhmmss_3661s);

    printf("\n--- timer_format: ss ---\n");
    RUN(ss_zero);
    RUN(ss_45s);
    RUN(ss_large);

    printf("\n--- timer_format: mm:ss.d ---\n");
    RUN(mmssd_zero);
    RUN(mmssd_90_7s);

    printf("\n--- timer_format: countdown remaining ---\n");
    RUN(countdown_remaining_default);
    RUN(countdown_remaining_mmss);

    printf("\n--- timer_format: countdown overtime ---\n");
    RUN(overtime_default);
    RUN(overtime_mmss);
    RUN(overtime_ss);
    RUN(overtime_mmssd);

    printf("\n--- timer_format: edge cases ---\n");
    RUN(unknown_format_fallback);
    RUN(stopped_timer_zero);
    RUN(all_timer_ids);
    RUN(small_buffer);

    printf("\n%d passed, %d failed\n", g_passed, g_tests - g_passed);
    return g_tests - g_passed;
}
