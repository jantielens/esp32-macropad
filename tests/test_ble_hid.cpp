#include "board_config.h"
#include "BLEDevice.h"
#if HAS_BLE_HID
#include "../src/app/ble_hid.cpp"
#endif
#include "../src/app/ble_backend.cpp"
#include "../src/app/bthome_events.cpp"
#include "../src/app/bthome_event_settings.cpp"
#include "../src/app/bthome_event_json.h"
#include <gtest/gtest.h>

unsigned long millis() { return TestBle::clock_ms; }
#if HAS_BLE_HID
void net_activity_mark(net_channel_t) {}
#endif
static bool ota_active = false;
static uint32_t ota_epoch = 1;
bool ota_activity_is_active() { return ota_active; }
uint32_t ota_activity_epoch() { return ota_epoch; }
uint32_t gamepad_hid_submission_generation() { return 1; }

class BleHid : public testing::Test {
protected:
    ble_gap_conn_desc peer = {7, {0, {1, 2, 3, 4, 5, 6}}, {0, {1, 2, 3, 4, 5, 6}}, {false, false}};

    void SetUp() override {
        BLEDevice::initialized = false;
        BLEDevice::init_succeeds = true;
        ota_active = false;
        ota_epoch = 1;
#if HAS_BLE_HID
        protocolMode = 1;
#endif
        TestBle::clock_ms = 100;
        TestBle::claimed = false;
        TestBle::owner.clear();
        TestBle::stored_bond = false;
        TestBle::clears = 0;
        TestBle::allocations = 0;
        TestBle::notifications = 0;
        TestBle::terminations.clear();
        TestBle::last_report.clear();
        TestBle::stop_succeeds = true;
        TestBle::event_start_result = 0;
        TestBle::hid_starts = 0;
        TestBle::event_starts = 0;
        TestBle::power_succeeds = TestBle::power_read_succeeds = true;
        TestBle::advertising_power = TestBle::default_power = ESP_PWR_LVL_N0;
        TestBle::power_calls.clear();
        queue_head = queue_count = 0;
        configured = false;
        event_active = false;
        radio_owned = false;
        packet_id = 0;
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
        NimBLEDevice::initialized = false;
#endif
        ASSERT_TRUE(ble_backend_configure_power(9));
#if HAS_BLE_HID
        ble_hid_init("Test", true);
#else
        NimBLEDevice::initialized = false;
        NimBLEDevice::init_succeeds = true;
        TestBle::radio_advertising = false;
#endif
        bthome_events_configure("Test");
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
        bthome_events_prepare();
#endif
    }

#if HAS_BLE_HID
    void connect() {
        peer.sec_state = {false, false};
        BLEDevice::server.callbacks->onConnect(&BLEDevice::server, &peer);
    }

    void authenticate(int status = 0, bool encrypted = true, bool bonded = true) {
        peer.sec_state = {encrypted, bonded};
        BLEDevice::security->onAuthenticationComplete(&peer, status);
    }

    void disconnect() {
        BLEDevice::server.callbacks->onDisconnect(&BLEDevice::server, &peer);
    }

    void pair_owner() {
        connect();
        authenticate();
        ASSERT_TRUE(ble_hid_is_connected());
        TestBle::stored_bond = true;
        TestBle::stored_peer = peer.peer_id_addr;
    }
#endif
};

TEST_F(BleHid, BthomeUsesStableTenButtonPositionsAndDistinctPackets) {
    ASSERT_TRUE(bthome_events_send(1, bthome::press));
    bthome_events_loop();
    ASSERT_EQ(TestBle::advertisement.size(), 30u);
    EXPECT_EQ(TestBle::advertisement,
        (std::vector<uint8_t>{2, 1, 6, 26, 22, 0xD2, 0xFC, 0x44, 0, 0,
            0x3A, 1, 0x3A, 0, 0x3A, 0, 0x3A, 0, 0x3A, 0,
            0x3A, 0, 0x3A, 0, 0x3A, 0, 0x3A, 0, 0x3A, 0}));
    const auto first_packet = TestBle::advertisement;
    EXPECT_EQ(TestBle::event_parameters.conn_mode, BLE_GAP_CONN_MODE_NON);
    TestBle::clock_ms += 349;
    bthome_events_loop();
    EXPECT_EQ(TestBle::event_starts, 1u);
    EXPECT_EQ(TestBle::advertisement, first_packet);
    ASSERT_TRUE(bthome_events_send(10, bthome::long_press));
    ++TestBle::clock_ms;
    bthome_events_loop();
    EXPECT_EQ(TestBle::advertisement[9], 1);
    EXPECT_EQ(TestBle::advertisement[11], 0);
    EXPECT_EQ(TestBle::advertisement[29], 4);
    ASSERT_TRUE(bthome_events_send(10, bthome::long_press));
    TestBle::clock_ms += 350;
    bthome_events_loop();
    EXPECT_EQ(TestBle::advertisement[9], 2);
    EXPECT_EQ(TestBle::advertisement[29], 4);
    TestBle::clock_ms += 350;
    bthome_events_loop();
    EXPECT_FALSE(event_active);
#if HAS_BLE_HID
    EXPECT_TRUE(TestBle::radio_advertising);
    EXPECT_EQ(TestBle::appearance, HID_KEYBOARD);
#else
    EXPECT_FALSE(TestBle::radio_advertising);
#endif
}

#if BLE_BACKEND == BLE_BACKEND_ARDUINO_CORE
TEST_F(BleHid, BthomeKeepsLazyInitializationOnOtherBoards) {
    BLEDevice::initialized = false;
    BLEDevice::init_succeeds = false;
    bthome_events_configure("Test");
    EXPECT_FALSE(BLEDevice::getInitialized());
    ASSERT_TRUE(bthome_events_send(1, bthome::press));
    bthome_events_loop();
    EXPECT_EQ(TestBle::event_starts, 0u);

    BLEDevice::init_succeeds = true;
    ASSERT_TRUE(bthome_events_send(1, bthome::press));
    bthome_events_loop();
    EXPECT_TRUE(BLEDevice::getInitialized());
    EXPECT_EQ(TestBle::event_starts, 1u);
}
#endif

#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
TEST_F(BleHid, BthomeRejectsFailedStartupAndPreparesWithoutAdvertising) {
    NimBLEDevice::initialized = false;
    NimBLEDevice::init_succeeds = false;
    bthome_events_configure("Test");
    ASSERT_FALSE(bthome_events_prepare());
    EXPECT_FALSE(bthome_events_send(1, bthome::press));
    bthome_events_loop();
    EXPECT_EQ(TestBle::event_starts, 0u);

    NimBLEDevice::init_succeeds = true;
    ASSERT_TRUE(bthome_events_prepare());
    ASSERT_TRUE(NimBLEDevice::isInitialized());
    const auto inits = TestBle::host_inits;
    ASSERT_TRUE(bthome_events_prepare());
    EXPECT_EQ(TestBle::host_inits, inits);
    EXPECT_EQ(TestBle::event_starts, 0u);
    EXPECT_FALSE(TestBle::radio_advertising);
    ASSERT_TRUE(bthome_events_send(1, bthome::press));
    bthome_events_loop();
    EXPECT_EQ(TestBle::event_starts, 1u);
}
#endif

TEST_F(BleHid, BthomeQueueRejectsNewestAndPreservesOrder) {
    for (uint8_t button = 1; button <= 4; ++button)
        ASSERT_TRUE(bthome_events_send(button, bthome::double_press));
    EXPECT_FALSE(bthome_events_send(5, bthome::press));
    EXPECT_FALSE(bthome_events_send(0, bthome::press));
    EXPECT_FALSE(bthome_events_send(11, bthome::press));
    EXPECT_FALSE(bthome_events_send(1, 3));
    for (uint8_t button = 1; button <= 4; ++button) {
        bthome_events_loop();
        ASSERT_EQ(TestBle::advertisement.size(), 30u);
        for (uint8_t position = 1; position <= 10; ++position)
            EXPECT_EQ(TestBle::advertisement[9 + 2 * position], position == button ? 2 : 0);
        TestBle::clock_ms += 350;
    }
    bthome_events_loop();
    EXPECT_EQ(TestBle::event_starts, 4u);
}

TEST_F(BleHid, BluetoothPowerAndTimingPersistTogetherAcrossFailedWrites) {
    Preferences preferences;
    auto loaded = bthome::load_bluetooth_settings(preferences, 9);
    EXPECT_EQ(loaded.tx_power_dbm, 9);
    EXPECT_EQ(loaded.events.burst_ms, 350);
    EXPECT_EQ(loaded.events.adv_interval_ms, 100);
    ASSERT_TRUE(bthome::save_bluetooth_settings(preferences, {-6, {1000, 300}}));
    loaded = bthome::load_bluetooth_settings(preferences, 9);
    EXPECT_EQ(loaded.tx_power_dbm, -6);
    EXPECT_EQ(loaded.events.burst_ms, 1000);
    EXPECT_EQ(loaded.events.adv_interval_ms, 300);
    preferences.write_succeeds = false;
    EXPECT_FALSE(bthome::save_bluetooth_settings(preferences, {3, {100, 20}}));
    loaded = bthome::load_bluetooth_settings(preferences, 9);
    EXPECT_EQ(loaded.tx_power_dbm, -6);
    EXPECT_EQ(loaded.events.burst_ms, 1000);
    EXPECT_EQ(loaded.events.adv_interval_ms, 300);
    preferences.write_succeeds = true;
    EXPECT_FALSE(bthome::save_bluetooth_settings(preferences, {1, {100, 20}}));
    EXPECT_FALSE(bthome::save_bluetooth_settings(preferences, {3, {100, 100}}));
    loaded = bthome::load_bluetooth_settings(preferences, 9);
    EXPECT_EQ(loaded.tx_power_dbm, -6);
    EXPECT_EQ(loaded.events.burst_ms, 1000);
}

TEST_F(BleHid, CorruptBluetoothRecordResetsPowerAndTimingTogether) {
    Preferences preferences;
    ASSERT_TRUE(bthome::save_bluetooth_settings(preferences, {-12, {1000, 300}}));
    for (const std::vector<uint8_t>& corrupt : {
            std::vector<uint8_t>{100, 0, 100, 0, 0},
            std::vector<uint8_t>{100, 0, 20, 0, 1},
            std::vector<uint8_t>{2, 0}}) {
        preferences.values.begin()->second = corrupt;
        const auto loaded = bthome::load_bluetooth_settings(preferences, 9);
        EXPECT_EQ(loaded.tx_power_dbm, 9);
        EXPECT_EQ(loaded.events.burst_ms, 350);
        EXPECT_EQ(loaded.events.adv_interval_ms, 100);
    }
}

TEST_F(BleHid, JsonSettingsValidateStrictIntegersAndMergedPartialTuples) {
    const bthome::EventSettings previous{350, 100};
    bthome::EventSettings candidate;
    for (const char* json : {
            "{\"bthome_event_burst_ms\":100,\"bthome_event_adv_interval_ms\":20}",
            "{\"bthome_event_burst_ms\":\"1000\",\"bthome_event_adv_interval_ms\":\"300\"}",
            "{\"bthome_event_burst_ms\":900}",
            "{\"bthome_event_adv_interval_ms\":116}"}) {
        JsonDocument doc;
        ASSERT_FALSE(deserializeJson(doc, json));
        EXPECT_EQ(bthome::parse_event_settings_update(doc.as<JsonObjectConst>(), previous, candidate), nullptr);
    }
    for (const char* key : {"bthome_event_burst_ms", "bthome_event_adv_interval_ms"}) {
        for (const char* value : {"true", "false", "null", "[]", "{}", "3.5", "\"\"", "\" \"",
                "\"3junk\"", "\"+3\"", "\"1e2\"", "\"2147483648\"", "\"-2147483649\"", "4294967296", "-4294967296"}) {
            std::string json = std::string("{\"") + key + "\":" + value + "}";
            JsonDocument doc;
            ASSERT_FALSE(deserializeJson(doc, json));
            candidate = previous;
            EXPECT_NE(bthome::parse_event_settings_update(doc.as<JsonObjectConst>(), previous, candidate), nullptr)
                << json;
            EXPECT_EQ(candidate.burst_ms, previous.burst_ms);
        }
    }
    for (const char* json : {"{\"bthome_event_burst_ms\":299}",
            "{\"bthome_event_adv_interval_ms\":117}", "{\"bthome_event_burst_ms\":1001}",
            "{\"bthome_event_adv_interval_ms\":19}", "{\"bthome_event_adv_interval_ms\":301}"}) {
        JsonDocument doc;
        ASSERT_FALSE(deserializeJson(doc, json));
        EXPECT_NE(bthome::parse_event_settings_update(doc.as<JsonObjectConst>(), previous, candidate), nullptr);
    }
}

TEST_F(BleHid, SettingsApplyOnlyToNextBurstAndExpireAcrossMillisWrap) {
    TestBle::clock_ms = UINT32_MAX - 150;
    ASSERT_TRUE(bthome_events_send(1, bthome::press));
    bthome_events_loop();
    EXPECT_EQ(TestBle::advertising_power, ESP_PWR_LVL_P9);
    EXPECT_EQ(TestBle::default_power, ESP_PWR_LVL_P9);
    const auto power_calls = TestBle::power_calls.size();
    EXPECT_EQ(TestBle::event_parameters.itvl_min, 160);
    ASSERT_TRUE(bthome_events_send(10, bthome::double_press));
    ASSERT_TRUE(bthome_events_apply_settings({900, 300}));
    TestBle::clock_ms += 349;
    bthome_events_loop();
    EXPECT_EQ(TestBle::event_starts, 1u);
    EXPECT_EQ(TestBle::advertising_power, ESP_PWR_LVL_P9);
    ++TestBle::clock_ms;
    bthome_events_loop();
    EXPECT_EQ(TestBle::event_starts, 2u);
    EXPECT_EQ(TestBle::advertising_power, ESP_PWR_LVL_P9);
    EXPECT_EQ(TestBle::event_parameters.itvl_min, 480);
    EXPECT_EQ(TestBle::event_parameters.itvl_max, 480);
    EXPECT_EQ(TestBle::advertisement[29], 2);
    TestBle::clock_ms += 899;
    bthome_events_loop();
    EXPECT_TRUE(event_active);
    ++TestBle::clock_ms;
    bthome_events_loop();
    EXPECT_FALSE(event_active);
    EXPECT_EQ(TestBle::advertising_power, ESP_PWR_LVL_P9);
    EXPECT_EQ(TestBle::default_power, ESP_PWR_LVL_P9);
    EXPECT_EQ(TestBle::power_calls.size(), power_calls);
}

TEST_F(BleHid, BootPowerSharedWithHidAndEventsCannotChangeUntilReboot) {
    BLEDevice::initialized = false;
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
    NimBLEDevice::initialized = false;
#endif
    ASSERT_TRUE(ble_backend_configure_power(-6));
    TestBle::power_calls.clear();
    EXPECT_FALSE(ble_backend_initialized());
    EXPECT_TRUE(TestBle::power_calls.empty());
#if HAS_BLE_HID
    ble_hid_init("Test", true);
#else
    ASSERT_TRUE(bthome_events_prepare());
#endif
    EXPECT_EQ(TestBle::advertising_power, ESP_PWR_LVL_N6);
    EXPECT_EQ(TestBle::default_power, ESP_PWR_LVL_N6);
    EXPECT_EQ(TestBle::scan_power, ESP_PWR_LVL_N6);
    EXPECT_FALSE(ble_backend_configure_power(9));
    EXPECT_EQ(ble_backend_tx_power_dbm(), -6);
    const auto inits = TestBle::host_inits;
    EXPECT_FALSE(bthome_events_apply_settings({350, 200}));
    EXPECT_EQ(TestBle::host_inits, inits);
    ASSERT_TRUE(bthome_events_send(1, bthome::press));
    bthome_events_loop();
    TestBle::clock_ms += 350;
    bthome_events_loop();
    EXPECT_EQ(TestBle::event_starts, 1u);
    EXPECT_EQ(TestBle::advertising_power, ESP_PWR_LVL_N6);
    EXPECT_EQ(TestBle::default_power, ESP_PWR_LVL_N6);
    EXPECT_EQ(TestBle::power_calls.size(), 3u);
}

TEST_F(BleHid, FailedStopKeepsRadioOwnedUntilRecovery) {
    ASSERT_TRUE(bthome_events_apply_settings({100, 20}));
    ASSERT_TRUE(bthome_events_send(1, bthome::press));
    bthome_events_loop();
    ASSERT_TRUE(bthome_events_send(2, bthome::press));
    TestBle::clock_ms += 100;
    TestBle::stop_succeeds = false;
    bthome_events_loop();
    EXPECT_TRUE(event_active);
    EXPECT_EQ(TestBle::advertising_power, ESP_PWR_LVL_P9);
    EXPECT_TRUE(radio_owned);
    EXPECT_EQ(TestBle::event_starts, 1u);
#if HAS_BLE_HID
    EXPECT_TRUE(advertising_paused);
#endif
    TestBle::stop_succeeds = true;
    bthome_events_loop();
    EXPECT_EQ(TestBle::event_starts, 2u);
    ota_active = true;
    ++ota_epoch;
    bthome_events_loop();
    EXPECT_FALSE(event_active);
    EXPECT_FALSE(radio_owned);
    EXPECT_EQ(TestBle::advertising_power, ESP_PWR_LVL_P9);
}

#if HAS_BLE_HID
TEST_F(BleHid, FailedHidRestorationBlocksNextEventUntilRetrySucceeds) {
    ASSERT_TRUE(bthome_events_send(1, bthome::press));
    bthome_events_loop();
    ASSERT_TRUE(bthome_events_send(2, bthome::press));
    TestBle::clock_ms += 350;
    TestBle::telemetry_start_succeeds = false;
    bthome_events_loop();
    EXPECT_FALSE(event_active);
    EXPECT_TRUE(radio_owned);
    EXPECT_EQ(TestBle::advertising_power, ESP_PWR_LVL_P9);
    EXPECT_EQ(TestBle::event_starts, 1u);
    TestBle::telemetry_start_succeeds = true;
    bthome_events_loop();
    EXPECT_TRUE(event_active);
    EXPECT_EQ(TestBle::event_starts, 2u);
}

TEST_F(BleHid, BthomeDoesNotInterruptConnectedHidAndDefersReconnectAdvertising) {
    pair_owner();
    ASSERT_TRUE(bthome_events_send(4, bthome::long_press));
    bthome_events_loop();
    EXPECT_TRUE(ble_hid_is_connected());
    EXPECT_TRUE(ble_hid_send_report(KS_USAGE_KEYBOARD, 4, 0));
    const auto event_packet = TestBle::advertisement;
    const auto starts = TestBle::hid_starts;
    disconnect();
    ble_hid_loop();
    EXPECT_EQ(TestBle::hid_starts, starts);
    EXPECT_EQ(TestBle::advertisement, event_packet);
    TestBle::clock_ms += 350;
    bthome_events_loop();
    EXPECT_EQ(TestBle::hid_starts, starts + 1);
    EXPECT_EQ(TestBle::appearance, HID_KEYBOARD);
}

TEST_F(BleHid, PairingRequestDoesNotReplaceActiveEventAdvertisement) {
    ASSERT_TRUE(bthome_events_send(10, bthome::double_press));
    bthome_events_loop();
    const auto packet = TestBle::advertisement;
    const auto starts = TestBle::hid_starts;
    ble_hid_request_pairing();
    ble_hid_loop();
    EXPECT_EQ(TestBle::advertisement, packet);
    EXPECT_EQ(TestBle::hid_starts, starts);
    EXPECT_TRUE(event_active);
    TestBle::clock_ms += 350;
    bthome_events_loop();
    EXPECT_GT(TestBle::hid_starts, starts);
}
#endif

TEST_F(BleHid, BthomeWorksWithoutHidInitializationAndCancelsStaleOtaEvents) {
#if HAS_BLE_HID
    hid_service_ready = false;
#endif
    ASSERT_TRUE(bthome_events_send(1, bthome::press));
    bthome_events_loop();
    EXPECT_TRUE(event_active);
    ASSERT_TRUE(bthome_events_send(2, bthome::press));
    ota_active = true;
    ++ota_epoch;
    EXPECT_FALSE(bthome_events_send(3, bthome::press));
    bthome_events_loop();
    EXPECT_FALSE(TestBle::radio_advertising);
    ota_active = false;
    EXPECT_EQ(TestBle::event_starts, 1u);
    ASSERT_TRUE(bthome_events_send(4, bthome::press));
    ++ota_epoch; // An update can begin and fail between main-loop ticks.
    bthome_events_loop();
    EXPECT_EQ(TestBle::event_starts, 1u);
}

TEST_F(BleHid, BthomeAdvertisingFailureRestoresHidAndStopFailureRetainsOwnership) {
    TestBle::event_start_result = 5;
    ASSERT_TRUE(bthome_events_send(1, bthome::press));
    bthome_events_loop();
    EXPECT_FALSE(event_active);
#if HAS_BLE_HID
    EXPECT_FALSE(advertising_paused);
    EXPECT_TRUE(TestBle::radio_advertising);
#else
    EXPECT_FALSE(TestBle::radio_advertising);
#endif
    TestBle::event_start_result = 0;
    ASSERT_TRUE(bthome_events_send(1, bthome::press));
    bthome_events_loop();
    TestBle::stop_succeeds = false;
    TestBle::clock_ms += 350;
    bthome_events_loop();
    EXPECT_TRUE(event_active);
#if HAS_BLE_HID
    EXPECT_TRUE(advertising_paused);
#endif
    TestBle::stop_succeeds = true;
    bthome_events_loop();
    EXPECT_FALSE(event_active);
#if HAS_BLE_HID
    EXPECT_FALSE(advertising_paused);
#endif
}

#if HAS_BLE_HID
TEST_F(BleHid, LegacyJustWorksAndEncryptedReportGate) {
    EXPECT_TRUE(TestBle::bonding);
    EXPECT_FALSE(TestBle::mitm);
    EXPECT_FALSE(TestBle::secure_connections);
    EXPECT_EQ(TestBle::capability, ESP_IO_CAP_NONE);
    connect();
    EXPECT_FALSE(ble_hid_is_connected());
    EXPECT_FALSE(ble_hid_send_report(KS_USAGE_KEYBOARD, 4, 0));
    EXPECT_FALSE(ble_hid_send_report(KS_USAGE_CONSUMER, 0xE9, 0));
    EXPECT_EQ(TestBle::allocations, 0u);
    authenticate();
    EXPECT_TRUE(ble_hid_is_connected());
    EXPECT_STREQ(ble_hid_status(), "connected");
    EXPECT_FALSE(ble_hid_is_pairing());
    EXPECT_TRUE(ble_hid_send_report(KS_USAGE_KEYBOARD, 4, 2));
    EXPECT_EQ(TestBle::last_report, (std::vector<uint8_t>{2, 0, 4, 0, 0, 0, 0, 0}));
    disconnect();
    EXPECT_FALSE(ble_hid_send_report(KS_USAGE_KEYBOARD, 0, 0));
    EXPECT_EQ(TestBle::notifications, 1u);
}

TEST_F(BleHid, EncryptedButUnbondedPeerIsNotReady) {
    connect();
    authenticate(0, true, false);
    EXPECT_FALSE(ble_hid_is_connected());
    EXPECT_FALSE(ble_hid_is_encrypted());
    EXPECT_EQ(TestBle::terminations.size(), 1u);
    EXPECT_FALSE(ble_hid_send_report(KS_USAGE_KEYBOARD, 4, 0));
}

TEST_F(BleHid, ReportCharacteristicsRequireSecuredOwner) {
    connect();
    unsigned report_count = 0;
    for (auto* characteristic = TestBle::services->characteristics; characteristic->uuid; ++characteristic) {
        if (characteristic->uuid->value != 0x2A4D) continue;
        ++report_count;
        os_mbuf buffer;
        ble_gatt_access_ctxt context = {BLE_GATT_ACCESS_OP_READ_CHR, &buffer};
        EXPECT_EQ(characteristic->access(peer.conn_handle, *characteristic->val_handle, &context,
            characteristic->arg), BLE_ATT_ERR_INSUFFICIENT_ENC);
        EXPECT_TRUE(buffer.bytes.empty());
    }
    EXPECT_EQ(report_count, 5u);
    authenticate();
    for (auto* characteristic = TestBle::services->characteristics; characteristic->uuid; ++characteristic) {
        if (characteristic->uuid->value != 0x2A4D) continue;
        os_mbuf buffer;
        ble_gatt_access_ctxt context = {BLE_GATT_ACCESS_OP_READ_CHR, &buffer};
        EXPECT_EQ(characteristic->access(peer.conn_handle + 1, *characteristic->val_handle, &context,
            characteristic->arg), BLE_ATT_ERR_INSUFFICIENT_ENC);
        EXPECT_EQ(characteristic->access(peer.conn_handle, *characteristic->val_handle, &context,
            characteristic->arg), 0);
        EXPECT_FALSE(buffer.bytes.empty());
    }
}

TEST_F(BleHid, DescriptorInputWidthsMatchWireReports) {
    uint32_t report_bits[5] = {};
    uint32_t report_id = 0;
    uint32_t report_size = 0;
    uint32_t report_count = 0;
    for (size_t offset = 0; offset < sizeof(HID_REPORT_MAP);) {
        const uint8_t prefix = HID_REPORT_MAP[offset++];
        ASSERT_NE(prefix, 0xFE);
        const size_t size = (prefix & 3) == 3 ? 4 : prefix & 3;
        ASSERT_LE(offset + size, sizeof(HID_REPORT_MAP));
        uint32_t value = 0;
        for (size_t byte = 0; byte < size; ++byte)
            value |= uint32_t(HID_REPORT_MAP[offset++]) << (8 * byte);
        switch (prefix & 0xFC) {
            case 0x84: report_id = value; break;
            case 0x74: report_size = value; break;
            case 0x94: report_count = value; break;
            case 0x80:
                ASSERT_GT(report_id, 0u);
                ASSERT_LT(report_id, 5u);
                report_bits[report_id] += report_size * report_count;
                break;
        }
    }
    EXPECT_EQ(report_bits[KEYBOARD_ID], sizeof(KeyReport) * 8);
    EXPECT_EQ(report_bits[MEDIA_KEYS_ID], sizeof(ConsumerReport) * 8);
    EXPECT_EQ(report_bits[MOUSE_ID], sizeof(BleMouseReport) * 8);
    EXPECT_EQ(report_bits[GAMEPAD_ID], sizeof(BleGamepadReport) * 8);
}

TEST_F(BleHid, MouseAndGamepadUseIndependentReports) {
    pair_owner();
    MouseHidReport mouse;
    mouse.buttons = 3;
    mouse.dx = -12;
    mouse.dy = 34;
    mouse.wheel = -1;
    mouse.pan = 2;
    ASSERT_TRUE(ble_hid_send_mouse_report(mouse, ble_hid_epoch(), ota_epoch));
    EXPECT_EQ(TestBle::last_report, (std::vector<uint8_t>{3, 244, 34, 255, 2}));
    GamepadReport gamepad;
    gamepad.left_x = -32767;
    gamepad.right_y = 32767;
    gamepad.left_trigger = 255;
    gamepad.hat = 8;
    gamepad.buttons = 0x8001;
    ASSERT_TRUE(ble_hid_send_gamepad_report(gamepad, ble_hid_epoch(), ota_epoch, 1));
    EXPECT_EQ(TestBle::last_report, (std::vector<uint8_t>{1, 128, 0, 0, 0, 0, 255, 127, 255, 0, 8, 1, 128}));
}

TEST_F(BleHid, PointerReportsRejectUnsecuredStaleAndOtaInput) {
    MouseHidReport mouse;
    mouse.dx = 1;
    GamepadReport gamepad;
    gamepad.buttons = 1;
    connect();
    EXPECT_FALSE(ble_hid_send_mouse_report(mouse, ble_hid_epoch(), ota_epoch));
    EXPECT_FALSE(ble_hid_send_gamepad_report(gamepad, ble_hid_epoch(), ota_epoch, 1));
    authenticate();
    EXPECT_FALSE(ble_hid_send_mouse_report(mouse, ble_hid_epoch() - 1, ota_epoch));
    EXPECT_FALSE(ble_hid_send_gamepad_report(gamepad, ble_hid_epoch() - 1, ota_epoch, 1));
    EXPECT_FALSE(ble_hid_send_mouse_report(mouse, ble_hid_epoch(), ota_epoch - 1));
    EXPECT_FALSE(ble_hid_send_gamepad_report(gamepad, ble_hid_epoch(), ota_epoch - 1, 1));
    EXPECT_FALSE(ble_hid_send_gamepad_report(gamepad, ble_hid_epoch(), ota_epoch, 2));
    ota_active = true;
    EXPECT_FALSE(ble_hid_send_mouse_report(mouse, ble_hid_epoch(), ota_epoch));
    EXPECT_FALSE(ble_hid_send_gamepad_report(gamepad, ble_hid_epoch(), ota_epoch, 1));
    EXPECT_EQ(TestBle::allocations, 0u);
    EXPECT_TRUE(ble_hid_send_mouse_report(MouseHidReport{}, ble_hid_epoch(), ota_epoch));
    EXPECT_TRUE(ble_hid_send_gamepad_report(GamepadReport{}, ble_hid_epoch(), ota_epoch, 1));
    protocolMode = 0;
    EXPECT_FALSE(ble_hid_send_mouse_report(MouseHidReport{}, ble_hid_epoch(), ota_epoch));
    EXPECT_FALSE(ble_hid_send_gamepad_report(GamepadReport{}, ble_hid_epoch(), ota_epoch, 1));
}

TEST_F(BleHid, ReconnectClearsCachedPointerReportsAndProtocol) {
    pair_owner();
    MouseHidReport mouse;
    mouse.buttons = 1;
    GamepadReport gamepad;
    gamepad.buttons = 1;
    ASSERT_TRUE(ble_hid_send_mouse_report(mouse, ble_hid_epoch(), ota_epoch));
    ASSERT_TRUE(ble_hid_send_gamepad_report(gamepad, ble_hid_epoch(), ota_epoch, 1));
    const uint32_t previous_epoch = ble_hid_epoch();
    protocolMode = 0;
    disconnect();
    connect();
    authenticate();
    EXPECT_EQ(protocolMode, 1);
    EXPECT_FALSE(ble_hid_send_mouse_report(mouse, previous_epoch, ota_epoch));
    EXPECT_FALSE(ble_hid_send_gamepad_report(gamepad, previous_epoch, ota_epoch, 1));
    for (void* token : {static_cast<void*>(&mouseInputToken), static_cast<void*>(&gamepadInputToken)}) {
        os_mbuf buffer;
        ble_gatt_access_ctxt context = {BLE_GATT_ACCESS_OP_READ_CHR, &buffer};
        ASSERT_EQ(hid_access(peer.conn_handle, 0, &context, token), 0);
        for (uint8_t value : buffer.bytes) EXPECT_EQ(value, 0);
    }
}

TEST_F(BleHid, OrdinaryFailurePreservesOwnerAndBond) {
    pair_owner();
    disconnect();
    const std::string owner = TestBle::owner;
    connect();
    authenticate(BLE_HS_HCI_ERR(0x08), false, true);
    EXPECT_TRUE(TestBle::claimed);
    EXPECT_EQ(TestBle::owner, owner);
    EXPECT_TRUE(TestBle::stored_bond);
    EXPECT_EQ(TestBle::clears, 1u);
    EXPECT_FALSE(ble_hid_is_pairing());
    EXPECT_FALSE(ble_hid_is_connected());
    EXPECT_FALSE(ble_hid_is_encrypted());
    authenticate();
    EXPECT_FALSE(ble_hid_is_connected());
}

TEST_F(BleHid, BondedOwnerReconnectsWithoutOpeningPairing) {
    pair_owner();
    disconnect();
    connect();
    EXPECT_TRUE(TestBle::terminations.empty());
    authenticate();
    EXPECT_TRUE(ble_hid_is_connected());
    EXPECT_EQ(TestBle::clears, 1u);
}

TEST_F(BleHid, EncryptionLossRevokesReportReadiness) {
    pair_owner();
    authenticate(BLE_HS_HCI_ERR(0x08), false, true);
    EXPECT_FALSE(ble_hid_is_connected());
    EXPECT_FALSE(ble_hid_is_encrypted());
    EXPECT_FALSE(ble_hid_send_report(KS_USAGE_KEYBOARD, 4, 0));
    EXPECT_EQ(TestBle::allocations, 0u);
    EXPECT_EQ(TestBle::terminations.size(), 1u);
    EXPECT_TRUE(TestBle::stored_bond);
}

TEST_F(BleHid, MissingKeyDoesNotAutomaticallyOpenPairing) {
    pair_owner();
    disconnect();
    connect();
    TestBle::stored_bond = false;
    authenticate(BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING), false, false);
    EXPECT_FALSE(ble_hid_is_pairing());
    EXPECT_TRUE(TestBle::claimed);
    EXPECT_EQ(TestBle::clears, 1u);
    disconnect();
    connect();
    EXPECT_EQ(TestBle::terminations.size(), 2u);
    authenticate();
    EXPECT_FALSE(ble_hid_is_connected());
}

TEST_F(BleHid, MissingKeyRecoveryIsBoundedAcrossReconnects) {
    connect();
    authenticate(BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING), false, false);
    EXPECT_TRUE(ble_hid_is_pairing());
    disconnect();
    connect();
    authenticate(BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING), false, false);
    EXPECT_FALSE(ble_hid_is_pairing());
    EXPECT_EQ(TestBle::clears, 1u);
    disconnect();
    ble_hid_request_pairing();
    ble_hid_loop();
    EXPECT_TRUE(ble_hid_is_pairing());
    EXPECT_EQ(TestBle::clears, 2u);
    connect();
    authenticate();
    EXPECT_TRUE(ble_hid_is_connected());
}

TEST_F(BleHid, NewPeerCannotClaimOutsidePairingWindow) {
    pair_owner();
    disconnect();
    const std::string owner = TestBle::owner;
    ++peer.peer_id_addr.val[0];
    connect();
    authenticate();
    EXPECT_FALSE(ble_hid_is_connected());
    EXPECT_EQ(TestBle::owner, owner);
    EXPECT_EQ(TestBle::terminations.size(), 1u);
}

TEST_F(BleHid, RepeatPairingRequiresExplicitWindow) {
    pair_owner();
    disconnect();
    connect();
    ble_gap_event event = {};
    event.type = BLE_GAP_EVENT_REPEAT_PAIRING;
    event.repeat_pairing.conn_handle = peer.conn_handle;
    TestBle::gap_handler(&event, nullptr);
    authenticate();
    EXPECT_FALSE(ble_hid_is_connected());
    EXPECT_FALSE(ble_hid_is_pairing());
    EXPECT_EQ(TestBle::terminations.size(), 1u);
}

TEST_F(BleHid, AuthenticationTimeoutRejectsLateSuccessOnce) {
    connect();
    TestBle::clock_ms += 9999;
    ble_hid_loop();
    EXPECT_TRUE(TestBle::terminations.empty());
    ++TestBle::clock_ms;
    ble_hid_loop();
    EXPECT_EQ(TestBle::terminations.size(), 1u);
    authenticate();
    EXPECT_FALSE(ble_hid_is_connected());
    TestBle::clock_ms += 10000;
    ble_hid_loop();
    EXPECT_EQ(TestBle::terminations.size(), 1u);
}

TEST_F(BleHid, ExpiredWindowRejectsLateAuthenticationWithoutLoopTick) {
    connect();
    TestBle::clock_ms += 60000;
    authenticate();
    EXPECT_FALSE(ble_hid_is_connected());
    EXPECT_FALSE(TestBle::claimed);
    EXPECT_EQ(TestBle::terminations.size(), 1u);
}

TEST_F(BleHid, PairingDeadlineSurvivesMillisWrap) {
    TestBle::clock_ms = UINT32_MAX - 1000;
    ble_hid_start_pairing();
    TestBle::clock_ms += 59999;
    ble_hid_loop();
    EXPECT_TRUE(ble_hid_is_pairing());
    ++TestBle::clock_ms;
    ble_hid_loop();
    EXPECT_FALSE(ble_hid_is_pairing());
}
#endif