#include "board_config.h"
#include "BLEDevice.h"
#include "../src/app/ble_hid.cpp"
#include <gtest/gtest.h>

unsigned long millis() { return TestBle::clock_ms; }
void net_activity_mark(net_channel_t) {}

class BleHid : public testing::Test {
protected:
    ble_gap_conn_desc peer = {7, {0, {1, 2, 3, 4, 5, 6}}, {0, {1, 2, 3, 4, 5, 6}}, {false, false}};

    void SetUp() override {
        TestBle::clock_ms = 100;
        TestBle::claimed = false;
        TestBle::owner.clear();
        TestBle::stored_bond = false;
        TestBle::clears = 0;
        TestBle::allocations = 0;
        TestBle::notifications = 0;
        TestBle::terminations.clear();
        TestBle::last_report.clear();
        ble_hid_init("Test", true);
    }

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
};

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
    EXPECT_EQ(report_count, 3u);
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