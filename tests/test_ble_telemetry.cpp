#include "board_config.h"
#include <string>

class String {
    std::string value;
public:
    String() = default;
    String(const char* text) : value(text) {}
    void concat(const char* bytes, size_t length) { value.append(bytes, length); }
    const char* c_str() const { return value.c_str(); }
    size_t length() const { return value.size(); }
};

#include "BLEDevice.h"
#include "../src/app/ble_backend.cpp"
#include "../src/app/ble_telemetry.cpp"
#include <gtest/gtest.h>

bool reboot_is_ota_mode() { return false; }

class BleTelemetry : public testing::Test {
protected:
    void SetUp() override {
        g_initialized = false;
        g_pending_count = 0;
        BLEDevice::initialized = false;
        BLEDevice::init_succeeds = true;
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
        NimBLEDevice::initialized = false;
        NimBLEDevice::init_succeeds = true;
#endif
        TestBle::radio_advertising = false;
        TestBle::stop_succeeds = true;
        TestBle::event_start_result = 0;
        TestBle::data_succeeds = true;
        TestBle::power_succeeds = true;
        TestBle::power_read_succeeds = true;
        TestBle::power_calls.clear();
        TestBle::advertising_power = TestBle::default_power = ESP_PWR_LVL_N0;
        TestBle::telemetry_start_succeeds = true;
        TestBle::clock_ms = 0;
        TestBle::host_inits = 0;
        ASSERT_TRUE(ble_backend_configure_power(9));
    }
};

TEST_F(BleTelemetry, FailedStartupAndPowerApplicationRejectAdvertising) {
    ASSERT_TRUE(ble_backend_configure_power(-12));
    EXPECT_FALSE(ble_backend_configure_power(7));
    BLEDevice::init_succeeds = false;
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
    NimBLEDevice::init_succeeds = false;
#endif
    ble_telemetry_init("Sensor");
    ble_telemetry_set_u8(1, 50);
    EXPECT_FALSE(ble_telemetry_advertise_burst(1, 100));
    BLEDevice::init_succeeds = true;
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
    NimBLEDevice::init_succeeds = true;
#endif
    TestBle::power_succeeds = false;
    ble_telemetry_init("Sensor");
    EXPECT_FALSE(ble_telemetry_advertise_burst(1, 100));
    TestBle::power_succeeds = true;
    TestBle::power_read_succeeds = false;
    ble_telemetry_init("Sensor");
    EXPECT_FALSE(ble_telemetry_advertise_burst(1, 100));
    TestBle::power_read_succeeds = true;
    ble_telemetry_init("Sensor");
    EXPECT_TRUE(ble_telemetry_advertise_burst(1, 100));
    EXPECT_EQ(TestBle::advertising_power, ESP_PWR_LVL_N12);
    EXPECT_EQ(TestBle::default_power, ESP_PWR_LVL_N12);
}

TEST_F(BleTelemetry, PreservesEmbeddedZerosAndSharedHostAcrossBursts) {
    ASSERT_TRUE(ble_backend_configure_power(3));
    ble_telemetry_init("Sensor");
    EXPECT_FALSE(TestBle::radio_advertising);
    EXPECT_EQ(TestBle::advertising_power, ESP_PWR_LVL_P3);
    EXPECT_EQ(TestBle::default_power, ESP_PWR_LVL_P3);
    EXPECT_EQ(TestBle::scan_power, ESP_PWR_LVL_P3);
    EXPECT_FALSE(ble_backend_configure_power(-12));
    EXPECT_EQ(ble_backend_tx_power_dbm(), 3);
    ble_telemetry_init("Sensor");
    EXPECT_EQ(TestBle::host_inits, 1u);
    ASSERT_TRUE(ble_telemetry_set_s16(2, 0));
    ASSERT_TRUE(ble_telemetry_set_u16(3, 256));
    ASSERT_TRUE(ble_telemetry_set_u8(1, 0));
    ASSERT_TRUE(ble_telemetry_advertise_burst(2, 100));
    EXPECT_EQ(TestBle::advertisement, (std::vector<uint8_t>{0x40, 2, 0, 0, 3, 0, 1, 1, 0}));
    EXPECT_EQ(TestBle::clock_ms, 250u);
    EXPECT_EQ(TestBle::event_parameters.itvl_min, 160);
    EXPECT_FALSE(TestBle::radio_advertising);
    ble_telemetry_deinit();
    EXPECT_TRUE(ble_backend_initialized());
    ble_telemetry_init("Sensor");
    EXPECT_EQ(TestBle::host_inits, 1u);
    EXPECT_EQ(TestBle::power_calls.size(), 3u);
}

TEST_F(BleTelemetry, AdvertisementFailuresAreReportedAndFailedStopCanBeRetried) {
    ble_telemetry_init("Sensor");
    ASSERT_TRUE(ble_telemetry_set_u8(1, 50));
    TestBle::data_succeeds = false;
    EXPECT_FALSE(ble_telemetry_advertise_burst(1, 100));
    TestBle::data_succeeds = true;
    TestBle::event_start_result = 5;
    TestBle::telemetry_start_succeeds = false;
    EXPECT_FALSE(ble_telemetry_advertise_burst(1, 100));
    EXPECT_EQ(TestBle::clock_ms, 0u);
    TestBle::event_start_result = 0;
    TestBle::telemetry_start_succeeds = true;
    TestBle::stop_succeeds = false;
    EXPECT_FALSE(ble_telemetry_advertise_burst(1, 100));
    EXPECT_TRUE(TestBle::radio_advertising);
    TestBle::stop_succeeds = true;
    ble_telemetry_deinit();
    EXPECT_FALSE(TestBle::radio_advertising);
}
