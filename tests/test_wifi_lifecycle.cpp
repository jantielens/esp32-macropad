#include "wifi_manager.h"
#include "web_portal_ap.h"
#include "ota_activity.h"
#include "power_manager.h"
#include "reboot_service.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <freertos/task.h>
#include <gtest/gtest.h>

unsigned long millis() { return test_wifi_now; }
size_t heap_caps_get_largest_free_block(uint32_t) { return 128 * 1024; }
static PowerMode current_power_mode = PowerMode::Offline;
static unsigned wifi_successes = 0, telemetry_refreshes = 0, reboot_requests = 0;
PowerMode power_manager_get_current_mode() { return current_power_mode; }
void power_manager_note_wifi_success() { ++wifi_successes; }
void device_telemetry_cache_rssi() { ++telemetry_refreshes; }
void reboot_request(uint32_t, RebootOwnership) { ++reboot_requests; }
const char* device_class_get_slug() { return "MACROPAD"; }

class WifiLifecycle : public testing::Test {
protected:
    DeviceConfig config = {};
    void tick() { wifi_manager_watchdog(&config, true, web_portal_is_ap_mode()); }
    WifiNetworkSnapshot snapshot() {
        WifiNetworkSnapshot value;
        wifi_manager_get_network_snapshot(&value);
        return value;
    }
    void start(WifiNetworkCommand command) {
        ASSERT_TRUE(wifi_manager_request_network_command(command));
        tick();
        ASSERT_NE(test_wifi_worker, nullptr);
        ASSERT_TRUE(test_wifi_run_worker());
        EXPECT_EQ(test_wifi_task_deletions,
            test_wifi_task_allocations - unsigned(test_wifi_task_waiting));
        EXPECT_EQ(test_wifi_task_stack, 6144U);
    }
    void connect() {
        start(WifiNetworkCommand::Connect);
        WiFi.association = WL_CONNECTED;
        WiFi.emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
        tick();
        EXPECT_TRUE(wifi_manager_network_transition_pending());
        ASSERT_TRUE(test_wifi_run_worker());
        tick();
        ASSERT_FALSE(wifi_manager_network_transition_pending());
        EXPECT_EQ(test_wifi_task_deletions, test_wifi_task_allocations);
        EXPECT_FALSE(test_wifi_thread.joinable());
        ASSERT_EQ(snapshot().state, WifiNetworkState::Connected);
    }
    void enable_ap() {
        start(WifiNetworkCommand::EnableAp);
        tick();
        ASSERT_TRUE(snapshot().ap_active);
        EXPECT_FALSE(web_portal_is_recovery_ap());
    }
    void SetUp() override {
        std::strcpy(config.wifi_ssid, "saved-network");
        std::strcpy(config.device_name, "desk-device");
        std::strcpy(config.ap_password, "safe-password");
        ota_activity_finish();
        ota_activity_finish_config_write();
        ota_activity_finish_network_transition();
        web_portal_stop_ap();
        wifi_manager_enter_offline();
        auto disconnected = WiFi.disconnected;
        auto got_ip = WiFi.got_ip;
        WiFi = TestLifecycleWiFi{};
        WiFi.disconnected = disconnected;
        WiFi.got_ip = got_ip;
        wifi_manager_register_events();
        test_wifi_task_creations = test_wifi_task_deletions = 0;
        test_wifi_task_allocations = 0;
        test_wifi_task_notifications = 0;
        test_wifi_task_allocation_fails = false;
        test_wifi_dns_start_ok = true;
        test_wifi_dns_starts = test_wifi_dns_stops = test_wifi_dns_requests = 0;
        test_wifi_delay_ms = 0;
        test_wifi_now += 1000;
        wifi_successes = telemetry_refreshes = reboot_requests = 0;
        current_power_mode = PowerMode::Offline;
        wifi_manager_init_network_snapshot(config);
        tick();
    }
    void TearDown() override {
        EXPECT_TRUE(test_wifi_run_worker());
        WiFi.association = WL_CONNECTED;
        tick();
        EXPECT_TRUE(test_wifi_run_worker());
        tick();
        ota_activity_finish();
        ota_activity_finish_config_write();
        web_portal_stop_ap();
        wifi_manager_enter_offline();
    }
};

TEST_F(WifiLifecycle, AdmissionSerializesCommandsAgainstOtaAndConfigWrites) {
    EXPECT_FALSE(wifi_manager_request_network_command(WifiNetworkCommand::None));
    ASSERT_TRUE(ota_activity_try_begin());
    EXPECT_FALSE(wifi_manager_request_network_command(WifiNetworkCommand::Connect));
    EXPECT_FALSE(wifi_manager_network_transition_pending());
    ota_activity_finish();
    ASSERT_TRUE(ota_activity_try_begin_config_write());
    EXPECT_FALSE(wifi_manager_request_network_command(WifiNetworkCommand::EnableAp));
    ota_activity_finish_config_write();
    ASSERT_TRUE(wifi_manager_request_network_command(WifiNetworkCommand::Connect));
    EXPECT_TRUE(wifi_manager_request_network_command(WifiNetworkCommand::Connect));
    EXPECT_FALSE(wifi_manager_request_network_command(WifiNetworkCommand::Disconnect));
    EXPECT_FALSE(ota_activity_try_begin());
    EXPECT_FALSE(ota_activity_try_begin_config_write());
    EXPECT_EQ(WiFi.begins, 0U);
    tick();
    EXPECT_EQ(test_wifi_task_creations, 1U);
    EXPECT_EQ(WiFi.begins, 0U);
    tick();
    EXPECT_EQ(test_wifi_task_creations, 1U);
    ASSERT_TRUE(test_wifi_run_worker());
    tick();
    EXPECT_TRUE(wifi_manager_network_transition_pending());
    EXPECT_FALSE(ota_activity_try_begin());
    WiFi.association = WL_CONNECTED;
    const unsigned mutations = WiFi.disconnects + WiFi.modes;
    tick();
    EXPECT_TRUE(wifi_manager_network_transition_pending());
    EXPECT_FALSE(ota_activity_try_begin());
    EXPECT_EQ(test_wifi_task_notifications, 1U);
    EXPECT_EQ(test_wifi_task_creations, 1U);
    EXPECT_EQ(test_wifi_task_deletions, 0U);
    EXPECT_EQ(WiFi.disconnects + WiFi.modes, mutations);
    ASSERT_TRUE(test_wifi_run_worker());
    tick();
    EXPECT_FALSE(wifi_manager_network_transition_pending());
    EXPECT_EQ(test_wifi_task_deletions, 1U);
    EXPECT_EQ(WiFi.disconnects + WiFi.modes, mutations);
    EXPECT_EQ(WiFi.getMode(), WIFI_STA);
    EXPECT_EQ(wifi_successes, 1U);
    EXPECT_EQ(telemetry_refreshes, 1U);
    EXPECT_FALSE(test_wifi_thread.joinable());
    ASSERT_TRUE(ota_activity_try_begin_config_write());
    ota_activity_finish_config_write();
    ASSERT_TRUE(ota_activity_try_begin());
    ota_activity_finish();
}

TEST_F(WifiLifecycle, StartupWorkerIsTransientAndAssociationTimeoutIsNonblocking) {
    WiFi.scan_duration_ms = 15000;
    ASSERT_TRUE(wifi_manager_request_network_command(WifiNetworkCommand::Connect));
    tick();
    EXPECT_EQ(snapshot().state, WifiNetworkState::Starting);
    EXPECT_EQ(WiFi.begins, 0U);
    EXPECT_EQ(test_wifi_task_stack, 6144U);
    ASSERT_TRUE(test_wifi_run_worker());
    EXPECT_TRUE(test_wifi_task_waiting);
    EXPECT_EQ(test_wifi_task_deletions, 0U);
    EXPECT_EQ(WiFi.begins, 1U);
    EXPECT_EQ(test_wifi_delay_ms, 0UL);
    tick();
    EXPECT_TRUE(wifi_manager_network_transition_pending());
    EXPECT_EQ(snapshot().state, WifiNetworkState::Connecting);
    test_wifi_now += 9999;
    tick();
    EXPECT_TRUE(wifi_manager_network_transition_pending());
    EXPECT_EQ(snapshot().state, WifiNetworkState::Connecting);
    test_wifi_now += 1;
    tick();
    tick();
    EXPECT_EQ(snapshot().state, WifiNetworkState::Stopping);
    EXPECT_EQ(test_wifi_delay_ms, 0UL);
    EXPECT_TRUE(wifi_manager_network_transition_pending());
    EXPECT_EQ(test_wifi_task_creations, 1U);
    EXPECT_EQ(test_wifi_task_deletions, 0U);
    EXPECT_EQ(test_wifi_task_notifications, 1U);
    EXPECT_EQ(WiFi.getMode(), WIFI_STA);
    EXPECT_FALSE(ota_activity_try_begin());
    ASSERT_NE(test_wifi_worker, nullptr);
    ASSERT_TRUE(test_wifi_run_worker());
    tick();
    EXPECT_EQ(test_wifi_task_deletions, 1U);
    EXPECT_FALSE(test_wifi_thread.joinable());
    EXPECT_FALSE(wifi_manager_network_transition_pending());
    EXPECT_EQ(snapshot().state, WifiNetworkState::Off);
    EXPECT_STREQ(snapshot().last_error, "Connection timed out");
    EXPECT_EQ(WiFi.getMode(), WIFI_OFF);
    EXPECT_FALSE(WiFi.auto_reconnect);
    EXPECT_EQ(wifi_successes, 0U);
    EXPECT_EQ(telemetry_refreshes, 0U);
    ASSERT_TRUE(ota_activity_try_begin());
    ota_activity_finish();
}

TEST_F(WifiLifecycle, ManualConnectionSelectsStrongestMatchingApAndReleasesScanResults) {
    WiFi.access_points = {
        {"saved-network", -75, 1, {1, 1, 1, 1, 1, 1}},
        {"other-network", -20, 6, {2, 2, 2, 2, 2, 2}},
        {"saved-network", -40, 11, {3, 3, 3, 3, 3, 3}}
    };
    connect();
    EXPECT_EQ(WiFi.scans, 1U);
    EXPECT_EQ(WiFi.begin_channel, 11);
    EXPECT_EQ(WiFi.begin_bssid, WiFi.access_points[2].bssid);
    EXPECT_FALSE(WiFi.scan_results_live);
}

TEST_F(WifiLifecycle, ManualConnectionFallsBackToSdkSelectionWhenScanCannotSelectAp) {
    WiFi.access_points = {{"other-network", -20, 6, {2, 2, 2, 2, 2, 2}}};
    connect();
    EXPECT_EQ(WiFi.begin_channel, 0);
    EXPECT_EQ(WiFi.begin_bssid, (std::array<uint8_t, 6>{}));
    EXPECT_FALSE(WiFi.scan_results_live);
    start(WifiNetworkCommand::Disconnect);
    tick();
    WiFi.scan_ok = false;
    connect();
    EXPECT_EQ(WiFi.scans, 2U);
    EXPECT_EQ(WiFi.begin_channel, 0);
    EXPECT_EQ(WiFi.begin_bssid, (std::array<uint8_t, 6>{}));
}

TEST_F(WifiLifecycle, TurnOffDisablesStationAndIsIdempotentWhenAlreadyOff) {
    connect();
    start(WifiNetworkCommand::TurnOff);
    tick();
    EXPECT_EQ(snapshot().state, WifiNetworkState::Off);
    EXPECT_EQ(WiFi.getMode(), WIFI_OFF);
    EXPECT_FALSE(WiFi.auto_reconnect);
    EXPECT_FALSE(wifi_manager_network_transition_pending());
    const unsigned mutations = WiFi.begins + WiFi.modes + WiFi.disconnects;
    const unsigned allocations = test_wifi_task_allocations;
    ASSERT_TRUE(wifi_manager_request_network_command(WifiNetworkCommand::TurnOff));
    tick();
    EXPECT_FALSE(wifi_manager_network_transition_pending());
    EXPECT_EQ(test_wifi_task_allocations, allocations);
    WiFi.association = WL_CONNECTED;
    WiFi.emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
    test_wifi_now += WIFI_REBOOT_AFTER_MS + 1;
    tick();
    EXPECT_EQ(snapshot().state, WifiNetworkState::Off);
    EXPECT_EQ(WiFi.begins + WiFi.modes + WiFi.disconnects, mutations);
    EXPECT_EQ(reboot_requests, 0U);
    EXPECT_EQ(current_power_mode, PowerMode::Offline);
}

TEST_F(WifiLifecycle, TurnOffStopsInteractiveAndRecoveryApIncludingCaptiveDns) {
    for (bool full_portal : {true, false}) {
        ASSERT_TRUE(web_portal_start_ap(&config, full_portal));
        tick();
        ASSERT_TRUE(snapshot().ap_active);
        start(WifiNetworkCommand::TurnOff);
        tick();
        EXPECT_EQ(snapshot().state, WifiNetworkState::Off);
        EXPECT_EQ(WiFi.getMode(), WIFI_OFF);
        EXPECT_FALSE(snapshot().ap_active);
        EXPECT_FALSE(web_portal_is_recovery_ap());
        EXPECT_FALSE(WiFi.auto_reconnect);
        EXPECT_STREQ(snapshot().ssid, "");
        EXPECT_STREQ(snapshot().ip, "");
        const unsigned dns_requests = test_wifi_dns_requests;
        web_portal_ap_handle();
        EXPECT_EQ(test_wifi_dns_requests, dns_requests);
        EXPECT_FALSE(wifi_manager_network_transition_pending());
    }
    EXPECT_EQ(test_wifi_dns_stops, 2U);
}

TEST_F(WifiLifecycle, AllocationFailurePreservesActiveApAndStation) {
    enable_ap();
    auto ap = snapshot();
    unsigned mutations = WiFi.modes + WiFi.disconnects + WiFi.ap_disconnects;
    test_wifi_task_allocation_fails = true;
    ASSERT_TRUE(wifi_manager_request_network_command(WifiNetworkCommand::Connect));
    tick();
    EXPECT_FALSE(wifi_manager_network_transition_pending());
    EXPECT_TRUE(web_portal_is_ap_mode());
    EXPECT_EQ(WiFi.getMode(), WIFI_AP);
    EXPECT_EQ(WiFi.modes + WiFi.disconnects + WiFi.ap_disconnects, mutations);
    EXPECT_STREQ(snapshot().ssid, ap.ssid);
    EXPECT_STREQ(snapshot().ip, ap.ip);
    EXPECT_STREQ(snapshot().last_error, "Radio startup allocation failed");
    EXPECT_EQ(test_wifi_task_deletions, 1U);
    test_wifi_task_allocation_fails = false;
    connect();
    mutations = WiFi.modes + WiFi.disconnects + WiFi.ap_disconnects;
    test_wifi_task_allocation_fails = true;
    ASSERT_TRUE(wifi_manager_request_network_command(WifiNetworkCommand::EnableAp));
    tick();
    EXPECT_EQ(snapshot().state, WifiNetworkState::Connected);
    EXPECT_EQ(WiFi.getMode(), WIFI_STA);
    EXPECT_TRUE(WiFi.auto_reconnect);
    EXPECT_EQ(WiFi.modes + WiFi.disconnects + WiFi.ap_disconnects, mutations);
    EXPECT_FALSE(wifi_manager_network_transition_pending());
    EXPECT_EQ(test_wifi_worker, nullptr);
}

TEST_F(WifiLifecycle, CrossSwitchesAndInactiveTransportCommandsRetainTheOtherTransport) {
    connect();
    const unsigned station_mutations = WiFi.modes + WiFi.disconnects;
    ASSERT_TRUE(wifi_manager_request_network_command(WifiNetworkCommand::DisableAp));
    tick();
    EXPECT_EQ(WiFi.modes + WiFi.disconnects, station_mutations);
    EXPECT_EQ(snapshot().state, WifiNetworkState::Connected);
    enable_ap();
    EXPECT_EQ(snapshot().state, WifiNetworkState::Ap);
    EXPECT_EQ(WiFi.getMode(), WIFI_AP);
    EXPECT_FALSE(WiFi.auto_reconnect);
    EXPECT_STREQ(snapshot().ip, "192.168.4.1");
    EXPECT_NE(std::strstr(snapshot().ssid, "ESP32-MACROPAD-"), nullptr);
    const unsigned ap_mutations = WiFi.modes + WiFi.disconnects + WiFi.ap_disconnects;
    ASSERT_TRUE(wifi_manager_request_network_command(WifiNetworkCommand::Disconnect));
    tick();
    EXPECT_EQ(WiFi.modes + WiFi.disconnects + WiFi.ap_disconnects, ap_mutations);
    EXPECT_TRUE(snapshot().ap_active);
    web_portal_ap_handle();
    EXPECT_EQ(test_wifi_dns_requests, 1U);
    ASSERT_TRUE(wifi_manager_request_network_command(WifiNetworkCommand::Connect));
    tick();
    EXPECT_FALSE(snapshot().ap_active);
    EXPECT_EQ(snapshot().state, WifiNetworkState::Starting);
    web_portal_ap_handle();
    EXPECT_EQ(test_wifi_dns_requests, 1U);
    ASSERT_TRUE(test_wifi_run_worker());
    EXPECT_FALSE(web_portal_is_ap_mode());
    EXPECT_EQ(test_wifi_dns_stops, 1U);
    WiFi.association = WL_CONNECTED;
    tick();
    ASSERT_TRUE(test_wifi_run_worker());
    tick();
    EXPECT_FALSE(snapshot().ap_active);
    EXPECT_STREQ(snapshot().ssid, "saved-network");
    EXPECT_STREQ(snapshot().ip, "192.168.1.42");
    EXPECT_EQ(snapshot().state, WifiNetworkState::Connected);
    EXPECT_TRUE(WiFi.auto_reconnect);
    EXPECT_EQ(current_power_mode, PowerMode::Offline);
}

TEST_F(WifiLifecycle, BootRecoveryAndRuntimeApHaveDistinctPurposesClearedOnStop) {
    ASSERT_TRUE(web_portal_start_ap(&config));
    tick();
    EXPECT_TRUE(web_portal_is_ap_mode());
    EXPECT_TRUE(web_portal_is_recovery_ap());
    EXPECT_TRUE(snapshot().ap_active);
    EXPECT_TRUE(snapshot().recovery_ap);
    char ssid[33], password[64], ip[16];
    ASSERT_TRUE(web_portal_ap_get_setup_info(ssid, sizeof(ssid),
        password, sizeof(password), ip, sizeof(ip)));
    EXPECT_STREQ(password, config.ap_password);
    start(WifiNetworkCommand::DisableAp);
    tick();
    EXPECT_FALSE(web_portal_is_ap_mode());
    EXPECT_FALSE(web_portal_is_recovery_ap());
    EXPECT_FALSE(snapshot().recovery_ap);
    EXPECT_FALSE(snapshot().ap_active);
    enable_ap();
    EXPECT_TRUE(web_portal_is_ap_mode());
    EXPECT_FALSE(web_portal_is_recovery_ap());
    ASSERT_TRUE(web_portal_ap_get_setup_info(ssid, sizeof(ssid),
        password, sizeof(password), ip, sizeof(ip)));
    EXPECT_STREQ(password, config.ap_password);
    connect();
    EXPECT_FALSE(web_portal_is_ap_mode());
    EXPECT_FALSE(web_portal_is_recovery_ap());
    EXPECT_FALSE(snapshot().ap_active);
}

TEST_F(WifiLifecycle, StartupFailuresFinishAdmissionAndDoNotLeaveSdkReconnectEnabled) {
    std::strcpy(config.wifi_ssid, "");
    start(WifiNetworkCommand::Connect);
    tick();
    EXPECT_STREQ(snapshot().last_error, "No saved SSID");
    EXPECT_FALSE(wifi_manager_network_transition_pending());
    EXPECT_EQ(WiFi.begins, 0U);
    EXPECT_EQ(WiFi.getMode(), WIFI_OFF);
    std::strcpy(config.wifi_ssid, "saved-network");
    WiFi.begin_result = WL_CONNECT_FAILED;
    start(WifiNetworkCommand::Connect);
    tick();
    EXPECT_FALSE(wifi_manager_network_transition_pending());
    EXPECT_STREQ(snapshot().last_error, "Station startup failed");
    EXPECT_FALSE(WiFi.auto_reconnect);
    EXPECT_EQ(WiFi.getMode(), WIFI_OFF);
    WiFi.begin_result = WL_IDLE_STATUS;
    std::strcpy(config.fixed_ip, "invalid-address");
    start(WifiNetworkCommand::Connect);
    tick();
    EXPECT_FALSE(wifi_manager_network_transition_pending());
    EXPECT_EQ(WiFi.begins, 1U);
    EXPECT_FALSE(WiFi.auto_reconnect);
    EXPECT_EQ(WiFi.getMode(), WIFI_OFF);
    ASSERT_TRUE(ota_activity_try_begin());
    ota_activity_finish();
}

TEST_F(WifiLifecycle, TimeoutCleanupNeedsNoAllocationAndReleasesTheRetainedWorker) {
    start(WifiNetworkCommand::Connect);
    test_wifi_task_allocation_fails = true;
    test_wifi_now += 10000;
    const unsigned mutations = WiFi.disconnects + WiFi.modes;
    tick();
    EXPECT_TRUE(wifi_manager_network_transition_pending());
    EXPECT_EQ(WiFi.disconnects + WiFi.modes, mutations);
    EXPECT_EQ(test_wifi_task_creations, 1U);
    EXPECT_EQ(test_wifi_task_allocations, 1U);
    EXPECT_EQ(test_wifi_task_deletions, 0U);
    EXPECT_EQ(test_wifi_task_notifications, 1U);
    EXPECT_FALSE(ota_activity_try_begin());
    ASSERT_TRUE(test_wifi_run_worker());
    tick();
    EXPECT_FALSE(wifi_manager_network_transition_pending());
    EXPECT_EQ(test_wifi_task_deletions, 1U);
    EXPECT_FALSE(test_wifi_thread.joinable());
    EXPECT_EQ(test_wifi_worker, nullptr);
    EXPECT_EQ(snapshot().state, WifiNetworkState::Off);
    EXPECT_STREQ(snapshot().last_error, "Connection timed out");
    EXPECT_EQ(WiFi.getMode(), WIFI_OFF);
    EXPECT_FALSE(WiFi.auto_reconnect);
    ASSERT_TRUE(ota_activity_try_begin());
    ota_activity_finish();
}

TEST_F(WifiLifecycle, IntentionalOfflineCannotReviveFromStaleEventsOrCachedSdkStatus) {
    connect();
    start(WifiNetworkCommand::Disconnect);
    tick();
    const unsigned mutations = WiFi.begins + WiFi.modes + WiFi.disconnects;
    const unsigned successes = wifi_successes;
    for (const auto event : {ARDUINO_EVENT_WIFI_STA_GOT_IP, ARDUINO_EVENT_WIFI_STA_DISCONNECTED}) {
        WiFi.association = WL_CONNECTED;
        WiFi.emit(event, 8);
        test_wifi_now += WIFI_REBOOT_AFTER_MS + 10000;
        tick();
        EXPECT_EQ(snapshot().state, WifiNetworkState::Off);
        EXPECT_EQ(WiFi.getMode(), WIFI_OFF);
        EXPECT_EQ(WiFi.begins + WiFi.modes + WiFi.disconnects, mutations);
        EXPECT_EQ(wifi_successes, successes);
        EXPECT_EQ(reboot_requests, 0U);
    }
    const unsigned begins = WiFi.begins;
    ASSERT_EQ(WiFi.status(), WL_CONNECTED);
    ASSERT_TRUE(wifi_manager_request_network_command(WifiNetworkCommand::Connect));
    tick();
    ASSERT_NE(test_wifi_worker, nullptr);
    EXPECT_TRUE(wifi_manager_network_transition_pending());
    EXPECT_EQ(WiFi.getMode(), WIFI_OFF);
    EXPECT_EQ(WiFi.begins, begins);
    EXPECT_EQ(wifi_successes, successes);
    ASSERT_TRUE(test_wifi_run_worker());
    EXPECT_EQ(WiFi.begins, begins + 1);
    EXPECT_EQ(WiFi.getMode(), WIFI_STA);
    EXPECT_NE(WiFi.status(), WL_CONNECTED);
    tick();
    EXPECT_TRUE(wifi_manager_network_transition_pending());
    EXPECT_EQ(snapshot().state, WifiNetworkState::Connecting);
    EXPECT_EQ(wifi_successes, successes);
    WiFi.association = WL_CONNECTED;
    WiFi.emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
    tick();
    ASSERT_TRUE(test_wifi_run_worker());
    tick();
    EXPECT_FALSE(wifi_manager_network_transition_pending());
    EXPECT_EQ(snapshot().state, WifiNetworkState::Connected);
    EXPECT_EQ(wifi_successes, successes + 1);
    EXPECT_EQ(test_wifi_task_deletions, test_wifi_task_allocations);
}

TEST_F(WifiLifecycle, SnapshotsAndDiagnosticsRemainSafeAfterDisconnect) {
    connect();
    WifiConnectionDiagnostics associated;
    wifi_manager_get_diagnostics(&associated);
    ASSERT_TRUE(associated.associated);
    ASSERT_EQ(associated.rssi, -52);
    char hostname[32];
    wifi_manager_get_mdns_hostname(hostname, sizeof(hostname));
    EXPECT_STREQ(hostname, "desk-device");
    WiFi.association = WL_DISCONNECTED;
    WiFi.emit(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, 201);
    tick();
    tick();
    WifiConnectionDiagnostics outage;
    const unsigned reads = WiFi.diagnostic_reads;
    WiFi.rssi = -100;
    wifi_manager_get_diagnostics(&outage);
    EXPECT_EQ(WiFi.diagnostic_reads, reads);
    EXPECT_FALSE(outage.associated);
    EXPECT_TRUE(outage.has_disconnect_reason);
    EXPECT_EQ(outage.disconnect_reason, 201);
    EXPECT_EQ(outage.rssi, associated.rssi);
    EXPECT_EQ(outage.channel, associated.channel);
    EXPECT_EQ(std::memcmp(outage.bssid, associated.bssid, 6), 0);
    auto retained = snapshot();
    EXPECT_EQ(retained.state, WifiNetworkState::Reconnecting);
    EXPECT_STREQ(retained.last_error, "Disconnected (reason 201)");
    start(WifiNetworkCommand::Disconnect);
    tick();
    wifi_manager_get_mdns_hostname(hostname, sizeof(hostname));
    EXPECT_STREQ(hostname, "");
    EXPECT_EQ(snapshot().state, WifiNetworkState::Off);
    EXPECT_STREQ(snapshot().last_error, retained.last_error);
    EXPECT_EQ(retained.state, WifiNetworkState::Reconnecting);
    wifi_manager_get_network_snapshot(nullptr);
    wifi_manager_get_diagnostics(nullptr);
    wifi_manager_get_mdns_hostname(nullptr, 0);
}

TEST_F(WifiLifecycle, StationDetailsRefreshEveryTwoSecondsAndImmediatelyAfterReconnect) {
    connect();
    const unsigned reads = WiFi.ssid_reads;
    EXPECT_STREQ(snapshot().ip, "192.168.1.42");
    WiFi.station_ip = IPAddress(192,168,1,99);
    for (unsigned i = 0; i < 199; ++i) {
        test_wifi_now += 10;
        tick();
    }
    EXPECT_EQ(WiFi.ssid_reads, reads);
    EXPECT_STREQ(snapshot().ip, "192.168.1.42");
    test_wifi_now += 10;
    tick();
    EXPECT_EQ(WiFi.ssid_reads, reads + 1);
    EXPECT_STREQ(snapshot().ip, "192.168.1.99");
    WiFi.association = WL_DISCONNECTED;
    tick();
    EXPECT_EQ(snapshot().state, WifiNetworkState::Reconnecting);
    EXPECT_STREQ(snapshot().ssid, "");
    EXPECT_STREQ(snapshot().ip, "");
    WiFi.station_ip = IPAddress(192,168,1,100);
    WiFi.association = WL_CONNECTED;
    WiFi.emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
    tick();
    EXPECT_EQ(snapshot().state, WifiNetworkState::Connected);
    EXPECT_EQ(WiFi.ssid_reads, reads + 2);
    EXPECT_STREQ(snapshot().ip, "192.168.1.100");
}

TEST_F(WifiLifecycle, HostnameSnapshotRetainsBootConfigurationUntilReinitialized) {
    EXPECT_STREQ(snapshot().hostname, "desk-device");
    connect();
    ASSERT_TRUE(ota_activity_try_begin_config_write());
    std::strcpy(config.device_name, "new-device");
    test_wifi_now += 2000;
    tick();
    EXPECT_STREQ(snapshot().hostname, "desk-device");
    ota_activity_finish_config_write();
    tick();
    EXPECT_STREQ(snapshot().hostname, "desk-device");
    start(WifiNetworkCommand::TurnOff);
    tick();
    EXPECT_STREQ(snapshot().hostname, "desk-device");
    wifi_manager_init_network_snapshot(config);
    tick();
    EXPECT_STREQ(snapshot().hostname, "new-device");
}

TEST_F(WifiLifecycle, OfflinePowerModeStillObservesRuntimeAssociationAndRecoversWithoutReboot) {
    connect();
    WiFi.association = WL_DISCONNECTED;
    WiFi.emit(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, 8);
    tick();
    unsigned begins = WiFi.begins;
    test_wifi_now += WIFI_TIER1_DURATION_MS;
    ASSERT_TRUE(ota_activity_try_begin());
    tick();
    EXPECT_EQ(WiFi.begins, begins);
    ota_activity_finish();
    tick();
    EXPECT_EQ(WiFi.begins, begins + 1);
    EXPECT_EQ(test_wifi_delay_ms, 0UL);
    const unsigned successes = wifi_successes;
    WiFi.emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
    tick();
    EXPECT_EQ(wifi_successes, successes);
    test_wifi_now += WIFI_REBOOT_AFTER_MS + 1;
    tick();
    EXPECT_EQ(reboot_requests, 0U);
    EXPECT_NE(snapshot().state, WifiNetworkState::Off);
    WiFi.association = WL_CONNECTED;
    WiFi.emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
    tick();
    tick();
    EXPECT_EQ(snapshot().state, WifiNetworkState::Connected);
    EXPECT_EQ(wifi_successes, successes + 1);
    EXPECT_EQ(telemetry_refreshes, wifi_successes);
    EXPECT_STREQ(snapshot().last_error, "");
}

TEST_F(WifiLifecycle, ApFailureAndTeardownDoNotPublishPartialSetupDetails) {
    test_wifi_dns_start_ok = false;
    start(WifiNetworkCommand::EnableAp);
    tick();
    EXPECT_FALSE(wifi_manager_network_transition_pending());
    EXPECT_FALSE(web_portal_is_ap_mode());
    EXPECT_FALSE(web_portal_is_recovery_ap());
    EXPECT_EQ(WiFi.getMode(), WIFI_OFF);
    EXPECT_FALSE(snapshot().ap_active);
    EXPECT_STREQ(snapshot().ssid, "");
    EXPECT_STREQ(snapshot().last_error, "AP startup failed");
    char ssid[4] = "old", ip[4] = "old";
    EXPECT_FALSE(web_portal_ap_get_network_info(ssid, sizeof(ssid), ip, sizeof(ip)));
    EXPECT_STREQ(ssid, "");
    EXPECT_STREQ(ip, "");
    test_wifi_dns_start_ok = true;
    enable_ap();
    EXPECT_TRUE(web_portal_ap_get_network_info(ssid, sizeof(ssid), ip, sizeof(ip)));
    EXPECT_EQ(ssid[3], '\0');
    EXPECT_EQ(ip[3], '\0');
    char password[64], full_ssid[33], full_ip[16];
    EXPECT_TRUE(web_portal_ap_get_setup_info(full_ssid, sizeof(full_ssid),
        password, sizeof(password), full_ip, sizeof(full_ip)));
    EXPECT_STREQ(password, config.ap_password);
    AsyncWebServer server;
    web_portal_ap_register_not_found(&server);
    AsyncWebServerRequest captive;
    server.not_found(&captive);
    EXPECT_EQ(captive.status, 302);
    EXPECT_EQ(captive.location, "/");
    start(WifiNetworkCommand::DisableAp);
    tick();
    EXPECT_FALSE(web_portal_is_ap_mode());
    EXPECT_FALSE(web_portal_is_recovery_ap());
    EXPECT_EQ(snapshot().state, WifiNetworkState::Off);
    EXPECT_STREQ(snapshot().ip, "");
    AsyncWebServerRequest missing;
    server.not_found(&missing);
    EXPECT_EQ(missing.status, 404);
    const unsigned disconnects = WiFi.ap_disconnects;
    web_portal_stop_ap();
    EXPECT_EQ(WiFi.ap_disconnects, disconnects);
}

TEST_F(WifiLifecycle, ShutdownFailureIsDistinctFromOffAndCanBeRetried) {
    connect();
    EXPECT_STREQ(snapshot().hostname, "desk-device");
    WiFi.mode_ok = false;
    start(WifiNetworkCommand::TurnOff);
    tick();
    EXPECT_FALSE(wifi_manager_network_transition_pending());
    EXPECT_EQ(snapshot().state, WifiNetworkState::Error);
    EXPECT_STREQ(snapshot().last_error, "Wi-Fi shutdown failed");
    EXPECT_STREQ(snapshot().ssid, "");
    WiFi.mode_ok = true;
    start(WifiNetworkCommand::TurnOff);
    tick();
    EXPECT_EQ(snapshot().state, WifiNetworkState::Off);
    EXPECT_EQ(WiFi.getMode(), WIFI_OFF);
    EXPECT_STREQ(snapshot().hostname, "desk-device");
    EXPECT_STREQ(snapshot().last_error, "Wi-Fi shutdown failed");
}
