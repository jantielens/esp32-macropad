// ============================================================================
// Unit tests for numeric rocker zone geometry
// ============================================================================

#include <cassert>
#include <cstdio>

#include "widgets/numericrocker_zones.h"

static int g_tests = 0;
static int g_passed = 0;

#define TEST(name) static void name()
#define RUN(name) do {                                       \
    g_tests++;                                               \
    printf("  %-50s ", #name);                              \
    name();                                                  \
    g_passed++;                                              \
    printf("PASS\n");                                      \
} while (0)

#define ASSERT_EQ(a, b) do {                                 \
    auto actual = (a); auto expected = (b);                  \
    if (actual != expected) {                                \
        printf("FAIL\n    %s:%d: %s != %s\n",              \
               __FILE__, __LINE__, #a, #b);                  \
        assert(false);                                       \
    }                                                        \
} while (0)

TEST(default_zones_scale_with_a_wide_button) {
    NRZoneLayout zones = nr_compute_zones(1000, 1.0f, 10.0f);
    ASSERT_EQ(zones.outer_end, 120);
    ASSERT_EQ(zones.inner_end, 270);
    ASSERT_EQ(zones.inner2_start, 730);
    ASSERT_EQ(zones.outer2_start, 880);
}

TEST(scale_setting_resizes_both_zone_types) {
    NRZoneLayout zones = nr_compute_zones(1000, 1.0f, 10.0f, 150);
    ASSERT_EQ(zones.outer_end, 180);
    ASSERT_EQ(zones.inner_end, 405);
    ASSERT_EQ(zones.inner2_start, 595);
    ASSERT_EQ(zones.outer2_start, 820);
}

TEST(scale_setting_is_clamped) {
    NRZoneLayout low = nr_compute_zones(1000, 1.0f, 10.0f, 0);
    NRZoneLayout high = nr_compute_zones(1000, 1.0f, 10.0f, 999);
    ASSERT_EQ(low.outer_end, 60);
    ASSERT_EQ(low.inner_end, 135);
    ASSERT_EQ(high.outer_end, 180);
    ASSERT_EQ(high.inner_end, 405);
}

TEST(center_zone_is_reserved_after_minimum_clamping) {
    NRZoneLayout zones = nr_compute_zones(100, 1.0f, 10.0f);
    ASSERT_EQ(zones.inner_end, 45);
    ASSERT_EQ(zones.inner2_start, 55);
}

TEST(disabled_inner_step_preserves_outer_zone_size) {
    NRZoneLayout zones = nr_compute_zones(1000, 0.0f, 10.0f);
    ASSERT_EQ(zones.outer_end, 120);
    ASSERT_EQ(zones.inner_end, 120);
    ASSERT_EQ(zones.inner2_start, 880);
    ASSERT_EQ(zones.outer2_start, 880);
}

int main() {
    printf("=== numeric rocker zones ===\n");
    RUN(default_zones_scale_with_a_wide_button);
    RUN(scale_setting_resizes_both_zone_types);
    RUN(scale_setting_is_clamped);
    RUN(center_zone_is_reserved_after_minimum_clamping);
    RUN(disabled_inner_step_preserves_outer_zone_size);
    printf("\n%d / %d tests passed\n", g_passed, g_tests);
    return g_passed == g_tests ? 0 : 1;
}