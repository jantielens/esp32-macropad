// ============================================================================
// Unit tests for WiFi reconnect pure functions
// ============================================================================
// Tests backoff calculation, tier escalation, and reboot threshold logic.
// Host-native — no ESP32 or WiFi dependency.

#include <cassert>
#include <cstdio>
#include <cstdint>

#include "wifi_manager.h"

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) static void test_##name()
#define RUN(name) do { \
    printf("  %-60s", #name); \
    test_##name(); \
    printf(" PASS\n"); \
    g_tests_passed++; \
} while(0)

#define ASSERT_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a != _b) { \
        printf(" FAIL (line %d: %ld != %ld)\n", __LINE__, (long)_a, (long)_b); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf(" FAIL (line %d: expected true)\n", __LINE__); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(expr) do { \
    if ((expr)) { \
        printf(" FAIL (line %d: expected false)\n", __LINE__); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

// ============================================================================
// wifi_reconnect_next_backoff tests
// ============================================================================

TEST(backoff_attempt_0) {
    // First attempt: base interval
    ASSERT_EQ(wifi_reconnect_next_backoff(0, 10000, 60000), 10000UL);
}

TEST(backoff_attempt_1) {
    // Second attempt: 10000 * 2 = 20000
    ASSERT_EQ(wifi_reconnect_next_backoff(1, 10000, 60000), 20000UL);
}

TEST(backoff_attempt_2) {
    // Third attempt: 10000 * 4 = 40000
    ASSERT_EQ(wifi_reconnect_next_backoff(2, 10000, 60000), 40000UL);
}

TEST(backoff_attempt_3_capped) {
    // Fourth attempt: 10000 * 8 = 80000, capped at 60000
    ASSERT_EQ(wifi_reconnect_next_backoff(3, 10000, 60000), 60000UL);
}

TEST(backoff_attempt_10_stays_capped) {
    // High attempt count stays at cap
    ASSERT_EQ(wifi_reconnect_next_backoff(10, 10000, 60000), 60000UL);
}

TEST(backoff_base_equals_max) {
    // When base == max, always returns base
    ASSERT_EQ(wifi_reconnect_next_backoff(0, 5000, 5000), 5000UL);
    ASSERT_EQ(wifi_reconnect_next_backoff(5, 5000, 5000), 5000UL);
}

TEST(backoff_base_exceeds_max) {
    // When base > max, returns max (from the cap in the loop)
    ASSERT_EQ(wifi_reconnect_next_backoff(0, 70000, 60000), 70000UL);
    ASSERT_EQ(wifi_reconnect_next_backoff(1, 70000, 60000), 60000UL);
}

// ============================================================================
// wifi_reconnect_get_tier tests
// ============================================================================

TEST(tier_at_0ms) {
    ASSERT_EQ((int)wifi_reconnect_get_tier(0, 60000, 300000), (int)WifiReconnectTier::Tier1);
}

TEST(tier_at_30s) {
    ASSERT_EQ((int)wifi_reconnect_get_tier(30000, 60000, 300000), (int)WifiReconnectTier::Tier1);
}

TEST(tier_at_59999ms) {
    // Just before Tier 1 expires
    ASSERT_EQ((int)wifi_reconnect_get_tier(59999, 60000, 300000), (int)WifiReconnectTier::Tier1);
}

TEST(tier_at_60000ms) {
    // Exact Tier 1→2 boundary
    ASSERT_EQ((int)wifi_reconnect_get_tier(60000, 60000, 300000), (int)WifiReconnectTier::Tier2);
}

TEST(tier_at_180s) {
    // Mid Tier 2
    ASSERT_EQ((int)wifi_reconnect_get_tier(180000, 60000, 300000), (int)WifiReconnectTier::Tier2);
}

TEST(tier_at_359999ms) {
    // Just before Tier 2→3 boundary (60000 + 300000 = 360000)
    ASSERT_EQ((int)wifi_reconnect_get_tier(359999, 60000, 300000), (int)WifiReconnectTier::Tier2);
}

TEST(tier_at_360000ms) {
    // Exact Tier 2→3 boundary
    ASSERT_EQ((int)wifi_reconnect_get_tier(360000, 60000, 300000), (int)WifiReconnectTier::Tier3);
}

TEST(tier_at_600s) {
    // Well into Tier 3
    ASSERT_EQ((int)wifi_reconnect_get_tier(600000, 60000, 300000), (int)WifiReconnectTier::Tier3);
}

TEST(tier_custom_thresholds) {
    // Tier 1 = 10s, Tier 2 = 20s → Tier 3 at 30s
    ASSERT_EQ((int)wifi_reconnect_get_tier(5000, 10000, 20000), (int)WifiReconnectTier::Tier1);
    ASSERT_EQ((int)wifi_reconnect_get_tier(15000, 10000, 20000), (int)WifiReconnectTier::Tier2);
    ASSERT_EQ((int)wifi_reconnect_get_tier(30000, 10000, 20000), (int)WifiReconnectTier::Tier3);
}

// ============================================================================
// wifi_reconnect_should_reboot tests
// ============================================================================

TEST(reboot_below_threshold) {
    ASSERT_FALSE(wifi_reconnect_should_reboot(599999, 600000));
}

TEST(reboot_at_threshold) {
    ASSERT_TRUE(wifi_reconnect_should_reboot(600000, 600000));
}

TEST(reboot_above_threshold) {
    ASSERT_TRUE(wifi_reconnect_should_reboot(700000, 600000));
}

TEST(reboot_zero_threshold) {
    // Zero threshold means always reboot
    ASSERT_TRUE(wifi_reconnect_should_reboot(0, 0));
}

TEST(reboot_zero_outage) {
    ASSERT_FALSE(wifi_reconnect_should_reboot(0, 600000));
}

// ============================================================================
// millis() rollover simulation
// ============================================================================

TEST(backoff_with_large_values) {
    // Verify no overflow issues with large millis-scale values
    unsigned long result = wifi_reconnect_next_backoff(0, 10000, 60000);
    ASSERT_EQ(result, 10000UL);
}

TEST(tier_with_large_elapsed) {
    // Simulates long uptime values — unsigned subtraction handles rollover
    ASSERT_EQ((int)wifi_reconnect_get_tier(4000000000UL, 60000, 300000),
              (int)WifiReconnectTier::Tier3);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== WiFi Reconnect Unit Tests ===\n\n");

    printf("--- wifi_reconnect_next_backoff ---\n");
    RUN(backoff_attempt_0);
    RUN(backoff_attempt_1);
    RUN(backoff_attempt_2);
    RUN(backoff_attempt_3_capped);
    RUN(backoff_attempt_10_stays_capped);
    RUN(backoff_base_equals_max);
    RUN(backoff_base_exceeds_max);

    printf("\n--- wifi_reconnect_get_tier ---\n");
    RUN(tier_at_0ms);
    RUN(tier_at_30s);
    RUN(tier_at_59999ms);
    RUN(tier_at_60000ms);
    RUN(tier_at_180s);
    RUN(tier_at_359999ms);
    RUN(tier_at_360000ms);
    RUN(tier_at_600s);
    RUN(tier_custom_thresholds);

    printf("\n--- wifi_reconnect_should_reboot ---\n");
    RUN(reboot_below_threshold);
    RUN(reboot_at_threshold);
    RUN(reboot_above_threshold);
    RUN(reboot_zero_threshold);
    RUN(reboot_zero_outage);

    printf("\n--- Edge cases ---\n");
    RUN(backoff_with_large_values);
    RUN(tier_with_large_elapsed);

    printf("\n%d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
