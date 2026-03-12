// ============================================================================
// Unit tests for widget common utilities (clamp_val, etc.)
// ============================================================================
// Compiled without LVGL / ArduinoJson — we pre-define the board_config guard
// so that widget.h skips display-only includes.

#include <cassert>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstdio>

// Block board_config.h and disable display/mqtt to avoid LVGL dependencies.
#define BOARD_CONFIG_H
#define HAS_DISPLAY 0
#define HAS_MQTT    0

#include "widgets/widget.h"

// ---------------------------------------------------------------------------
// Minimal test harness
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

#define ASSERT_EQ(a, b) do {                                 \
    auto _a = (a); auto _b = (b);                            \
    if (_a != _b) {                                          \
        printf("FAIL\n    %s:%d: %s != %s\n",               \
               __FILE__, __LINE__, #a, #b);                  \
        assert(false);                                       \
    }                                                        \
} while (0)

#define ASSERT_FLOAT_EQ(a, b) do {                           \
    float _a = (a); float _b = (b);                          \
    float _d = (_a > _b) ? (_a - _b) : (_b - _a);           \
    if (_d > 1e-6f) {                                        \
        printf("FAIL\n    %s:%d: %s (%.6f) != %s (%.6f)\n", \
               __FILE__, __LINE__, #a, _a, #b, _b);         \
        assert(false);                                       \
    }                                                        \
} while (0)

// ===========================================================================
// clamp_val<int>
// ===========================================================================
TEST(clamp_int_mid)          { ASSERT_EQ(clamp_val(5, 0, 10), 5); }
TEST(clamp_int_below)        { ASSERT_EQ(clamp_val(-3, 0, 10), 0); }
TEST(clamp_int_above)        { ASSERT_EQ(clamp_val(15, 0, 10), 10); }
TEST(clamp_int_at_lo)        { ASSERT_EQ(clamp_val(0, 0, 10), 0); }
TEST(clamp_int_at_hi)        { ASSERT_EQ(clamp_val(10, 0, 10), 10); }
TEST(clamp_int_negative_range) { ASSERT_EQ(clamp_val(-5, -10, -1), -5); }
TEST(clamp_int_single_point) { ASSERT_EQ(clamp_val(99, 7, 7), 7); }

// ===========================================================================
// clamp_val<uint8_t>  (matches real widget usage)
// ===========================================================================
TEST(clamp_u8_mid)           { ASSERT_EQ(clamp_val<uint8_t>(50, 1, 100), (uint8_t)50); }
TEST(clamp_u8_below)         { ASSERT_EQ(clamp_val<uint8_t>(0, 1, 100), (uint8_t)1); }
TEST(clamp_u8_above)         { ASSERT_EQ(clamp_val<uint8_t>(200, 1, 100), (uint8_t)100); }
TEST(clamp_u8_at_lo)         { ASSERT_EQ(clamp_val<uint8_t>(1, 1, 100), (uint8_t)1); }
TEST(clamp_u8_at_hi)         { ASSERT_EQ(clamp_val<uint8_t>(100, 1, 100), (uint8_t)100); }

// Exact ranges from widget parse functions
TEST(clamp_u8_bar_width)     { ASSERT_EQ(clamp_val<uint8_t>(0, 1, 100), (uint8_t)1); }
TEST(clamp_u8_arc_width)     { ASSERT_EQ(clamp_val<uint8_t>(3, 5, 50), (uint8_t)5); }
TEST(clamp_u8_tick_width)    { ASSERT_EQ(clamp_val<uint8_t>(0, 1, 5), (uint8_t)1); }
TEST(clamp_u8_tick_width_hi) { ASSERT_EQ(clamp_val<uint8_t>(9, 1, 5), (uint8_t)5); }

// ===========================================================================
// clamp_val<float>
// ===========================================================================
TEST(clamp_float_mid)        { ASSERT_FLOAT_EQ(clamp_val(3.14f, 0.0f, 10.0f), 3.14f); }
TEST(clamp_float_below)      { ASSERT_FLOAT_EQ(clamp_val(-0.5f, 0.0f, 10.0f), 0.0f); }
TEST(clamp_float_above)      { ASSERT_FLOAT_EQ(clamp_val(99.9f, 0.0f, 10.0f), 10.0f); }
TEST(clamp_float_at_lo)      { ASSERT_FLOAT_EQ(clamp_val(0.0f, 0.0f, 10.0f), 0.0f); }
TEST(clamp_float_at_hi)      { ASSERT_FLOAT_EQ(clamp_val(10.0f, 0.0f, 10.0f), 10.0f); }

// ===========================================================================
// clamp_val<uint16_t>  (gauge arc_degrees uses int → uint16_t cast)
// ===========================================================================
TEST(clamp_u16_mid)          { ASSERT_EQ(clamp_val<uint16_t>(180, 10, 360), (uint16_t)180); }
TEST(clamp_u16_below)        { ASSERT_EQ(clamp_val<uint16_t>(5, 10, 360), (uint16_t)10); }
TEST(clamp_u16_above)        { ASSERT_EQ(clamp_val<uint16_t>(500, 10, 360), (uint16_t)360); }

// ===========================================================================
// clamp_val with int (gauge degrees path: clamp_val(deg, 10, 360))
// ===========================================================================
TEST(clamp_degrees_mid)      { ASSERT_EQ(clamp_val(180, 10, 360), 180); }
TEST(clamp_degrees_lo)       { ASSERT_EQ(clamp_val(5, 10, 360), 10); }
TEST(clamp_degrees_hi)       { ASSERT_EQ(clamp_val(999, 10, 360), 360); }

// ===========================================================================
int main() {
    printf("=== widget_common tests ===\n");

    // int
    RUN(clamp_int_mid);
    RUN(clamp_int_below);
    RUN(clamp_int_above);
    RUN(clamp_int_at_lo);
    RUN(clamp_int_at_hi);
    RUN(clamp_int_negative_range);
    RUN(clamp_int_single_point);

    // uint8_t
    RUN(clamp_u8_mid);
    RUN(clamp_u8_below);
    RUN(clamp_u8_above);
    RUN(clamp_u8_at_lo);
    RUN(clamp_u8_at_hi);
    RUN(clamp_u8_bar_width);
    RUN(clamp_u8_arc_width);
    RUN(clamp_u8_tick_width);
    RUN(clamp_u8_tick_width_hi);

    // float
    RUN(clamp_float_mid);
    RUN(clamp_float_below);
    RUN(clamp_float_above);
    RUN(clamp_float_at_lo);
    RUN(clamp_float_at_hi);

    // uint16_t
    RUN(clamp_u16_mid);
    RUN(clamp_u16_below);
    RUN(clamp_u16_above);

    // int (degrees path)
    RUN(clamp_degrees_mid);
    RUN(clamp_degrees_lo);
    RUN(clamp_degrees_hi);

    printf("\n%d / %d tests passed\n", g_passed, g_tests);
    return (g_passed == g_tests) ? 0 : 1;
}
