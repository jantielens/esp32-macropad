// ============================================================================
// Unit tests for the network activity tracker (net_activity)
// ============================================================================
// Host-native: compiled with a controllable millis() mock so we can drive the
// age / active-window logic deterministically.

#include <cstdio>
#include <cstring>

#include "net_activity.h"

// ---------------------------------------------------------------------------
// Controllable millis() mock (Arduino.h in tests/ declares this extern "C")
// ---------------------------------------------------------------------------
static unsigned long s_millis = 0;
extern "C" unsigned long millis() { return s_millis; }

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char* label) {
    if (cond) {
        g_pass++;
        std::printf("  PASS  %s\n", label);
    } else {
        g_fail++;
        std::printf("  FAIL  %s\n", label);
    }
}

static void check_eq_u(uint32_t got, uint32_t expected, const char* label) {
    if (got == expected) {
        g_pass++;
        std::printf("  PASS  %s\n", label);
    } else {
        g_fail++;
        std::printf("  FAIL  %s (got %u, expected %u)\n", label,
                    (unsigned)got, (unsigned)expected);
    }
}

static void test_never_before_mark() {
    s_millis = 5000;
    check_eq_u(net_activity_age_ms(NET_CH_OTA), NET_ACTIVITY_NEVER,
               "unmarked channel reports NEVER");
    check(!net_activity_is_active(NET_CH_OTA, 1000),
          "unmarked channel is not active");
}

static void test_mark_and_age() {
    s_millis = 10000;
    net_activity_mark(NET_CH_PORTAL);
    check_eq_u(net_activity_age_ms(NET_CH_PORTAL), 0, "age is 0 right after mark");

    s_millis = 10250;
    check_eq_u(net_activity_age_ms(NET_CH_PORTAL), 250, "age tracks elapsed time");
    check(net_activity_is_active(NET_CH_PORTAL, 400), "active within window");

    s_millis = 10500;
    check(!net_activity_is_active(NET_CH_PORTAL, 400), "inactive past window");
}

static void test_rollover() {
    // Mark just before the millis() wrap, then read after the wrap.
    s_millis = 0xFFFFFF00u;
    net_activity_mark(NET_CH_MQTT_RX);
    s_millis = 0x00000030u;  // 0x130 ms later, across the 32-bit boundary
    check_eq_u(net_activity_age_ms(NET_CH_MQTT_RX), 0x130u,
               "age is correct across millis() rollover");
}

static void test_any_aggregate() {
    // Fresh channels are hard to isolate across tests, so just prove that a
    // recent mark makes the aggregate active and dominates the min-age.
    s_millis = 20000;
    net_activity_mark(NET_CH_MCP);
    check_eq_u(net_activity_age_any_ms(), 0, "any aggregate is 0 after a fresh mark");

    s_millis = 20100;
    net_activity_mark(NET_CH_BLE);
    s_millis = 20150;
    check_eq_u(net_activity_age_any_ms(), 50,
               "any aggregate returns the most recent channel's age");
}

static void test_mqtt_aggregate() {
    // mqtt aggregate = min(mqtt_rx, mqtt_tx), independent of other channels.
    s_millis = 30000;
    net_activity_mark(NET_CH_MQTT_TX);
    s_millis = 30040;
    check_eq_u(net_activity_age_mqtt_ms(), 40, "mqtt aggregate tracks mqtt_tx");

    s_millis = 30100;
    net_activity_mark(NET_CH_MQTT_RX);
    s_millis = 30110;
    check_eq_u(net_activity_age_mqtt_ms(), 10,
               "mqtt aggregate returns the more recent of rx/tx");

    // A non-MQTT mark must not affect the mqtt aggregate.
    s_millis = 30120;
    net_activity_mark(NET_CH_HTTP);
    s_millis = 30130;
    check_eq_u(net_activity_age_mqtt_ms(), 30,
               "mqtt aggregate ignores non-MQTT channels");
}

static void test_channel_names() {
    check(net_activity_channel_from_name("portal") == NET_CH_PORTAL, "name portal");
    check(net_activity_channel_from_name("mcp") == NET_CH_MCP, "name mcp");
    check(net_activity_channel_from_name("mqtt_rx") == NET_CH_MQTT_RX, "name mqtt_rx");
    check(net_activity_channel_from_name("mqtt_tx") == NET_CH_MQTT_TX, "name mqtt_tx");
    check(net_activity_channel_from_name("http") == NET_CH_HTTP, "name http");
    check(net_activity_channel_from_name("ble") == NET_CH_BLE, "name ble");
    check(net_activity_channel_from_name("ota") == NET_CH_OTA, "name ota");
    check(net_activity_channel_from_name("mqtt") == NET_CH_AGG_MQTT, "name mqtt -> aggregate");
    check(net_activity_channel_from_name("any") == NET_CH_AGG_ANY, "name any -> aggregate");
    check(net_activity_channel_from_name("bogus") == -1, "unknown name -> -1");
    check(net_activity_channel_from_name("") == -1, "empty name -> -1");
    check(net_activity_channel_from_name(nullptr) == -1, "null name -> -1");
}

static void test_out_of_range() {
    net_activity_mark((net_channel_t)999);  // must not crash
    check_eq_u(net_activity_age_ms((net_channel_t)999), NET_ACTIVITY_NEVER,
               "out-of-range channel reports NEVER");
}

int main() {
    std::printf("=== net_activity tests ===\n\n");

    test_never_before_mark();
    test_mark_and_age();
    test_rollover();
    test_any_aggregate();
    test_mqtt_aggregate();
    test_channel_names();
    test_out_of_range();

    std::printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
