#include "power_manager.h"
#include "device_class.h"
#include "ota_activity.h"
#include "action_registry.h"
#include "main_loop_bridge.h"

#include <Preferences.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <gtest/gtest.h>

static unsigned restart_calls = 0;
static const ActionTypeDef* system_action = nullptr;
struct TestPowerEsp {
    void restart() { ++restart_calls; }
} ESP;

void action_type_register(const ActionTypeDef* type) { system_action = type; }
void wifi_manager_request_reconnect() {}

static LoopBridgeExec queued_job = nullptr;
static LoopBridgeResult enqueue_result = LOOP_BRIDGE_OK;
LoopBridgeResult loop_bridge_enqueue(LoopBridgeExec exec, const void*, size_t context_len) {
    EXPECT_EQ(context_len, 0U);
    if (enqueue_result == LOOP_BRIDGE_OK) queued_job = exec;
    return enqueue_result;
}

// Exercise the firmware dispatch branch without enabling ArduinoJson's SDK adapters.
#define ARDUINO 1
#include "../src/app/actions/system_action.cpp"
#undef ARDUINO

unsigned long millis() { return 0; }
bool time_service_localtime(time_t epoch, struct tm* result) {
    return localtime_r(&epoch, result) != nullptr;
}

TEST(PowerManager, RecoveryAndRestartLifecycle) {
    power_manager_boot_init();
    ASSERT_TRUE(test_power_preferences["armed"]);
    EXPECT_EQ(test_power_timer_duration, 10000000ULL);
    power_manager_loop();
    EXPECT_TRUE(test_power_preferences["armed"]);

    // A second power-on before expiry consumes recovery exactly once.
    power_manager_boot_init();
    EXPECT_TRUE(power_manager_should_force_setup_ap());
    EXPECT_FALSE(test_power_preferences["armed"]);

    // Expiry must also work while setup is blocking, without a loop iteration.
    power_manager_boot_init();
    ASSERT_TRUE(test_power_preferences["armed"]);
    test_power_timer.callback(nullptr);
    EXPECT_FALSE(test_power_preferences["armed"]);
    power_manager_boot_init();
    EXPECT_FALSE(power_manager_should_force_setup_ap());
    ASSERT_TRUE(test_power_preferences["armed"]);

    ASSERT_TRUE(ota_activity_try_begin());
    test_power_timer.callback(nullptr);
    power_manager_loop();
    EXPECT_TRUE(test_power_preferences["armed"]);
    ota_activity_finish();
    test_power_write_fails = true;
    power_manager_loop();
    EXPECT_TRUE(test_power_preferences["armed"]);
    test_power_write_fails = false;
    power_manager_loop();
    EXPECT_FALSE(test_power_preferences["armed"]);

    power_manager_boot_init();
    test_power_write_fails = true;
    test_power_timer.callback(nullptr);
    test_power_write_fails = false;
    power_manager_loop();
    EXPECT_FALSE(test_power_preferences["armed"]);

    power_manager_boot_init();
    ASSERT_TRUE(test_power_preferences["armed"]);
    power_manager_sleep_for(1);
    EXPECT_TRUE(test_power_slept);
    EXPECT_FALSE(test_power_timer_running);
    EXPECT_FALSE(test_power_preferences["armed"]);
    test_power_reset_reason = ESP_RST_DEEPSLEEP;
    power_manager_boot_init();
    EXPECT_FALSE(power_manager_should_force_setup_ap());
    EXPECT_FALSE(test_power_preferences["armed"]);

    test_power_reset_reason = ESP_RST_POWERON;
    power_manager_boot_init();
    test_power_reset_reason = ESP_RST_PANIC;
    power_manager_boot_init();
    EXPECT_FALSE(power_manager_should_force_setup_ap());
    EXPECT_FALSE(test_power_preferences["armed"]);

    ASSERT_TRUE(ota_activity_try_begin_config_write());
    EXPECT_FALSE(power_manager_request_setup_ap_restart());
    ota_activity_finish_config_write();
    test_power_write_fails = true;
    EXPECT_FALSE(power_manager_request_setup_ap_restart());
    test_power_write_fails = false;
    ASSERT_TRUE(ota_activity_try_begin());
    EXPECT_FALSE(power_manager_request_setup_ap_restart());
    ota_activity_finish();
    ASSERT_TRUE(power_manager_request_setup_ap_restart());
    EXPECT_TRUE(test_power_preferences["req_ap"]);
    EXPECT_FALSE(ota_activity_try_begin());
    EXPECT_FALSE(ota_activity_try_begin_config_write());
    ota_activity_finish_network_transition(); // Simulate reset of volatile state.
    test_power_reset_reason = ESP_RST_SW;
    power_manager_boot_init();
    EXPECT_TRUE(power_manager_should_force_setup_ap());
    EXPECT_FALSE(test_power_preferences["req_ap"]);
    power_manager_boot_init();
    EXPECT_FALSE(power_manager_should_force_setup_ap());
}

TEST(DeviceClass, SetupRegistrationAndNormalRuntimeAreSeparate) {
    static unsigned setup_calls = 0;
    static unsigned runtime_calls = 0;
    static DeviceClass cls = {};
    cls.name = "test-runtime";
    cls.on_setup_late = [](DeviceConfig*, PowerMode) { ++setup_calls; };
    cls.on_normal_runtime = [](DeviceConfig*) { ++runtime_calls; };
    ASSERT_TRUE(device_class_register(&cls));
    DeviceConfig config = {};
    device_class_dispatch_setup_late(&config, PowerMode::Ap);
    EXPECT_EQ(setup_calls, 1U);
    EXPECT_EQ(runtime_calls, 0U);
    device_class_dispatch_normal_runtime(&config);
    EXPECT_EQ(setup_calls, 1U);
    EXPECT_EQ(runtime_calls, 1U);
}

TEST(SystemAction, SetupApRestartRunsOnlyInQueuedJob) {
    ASSERT_NE(system_action, nullptr);
    ButtonAction action = {};
    std::strcpy(action.payload.system.system_command, "reboot_ap");
    test_power_preferences["req_ap"] = false;
    ASSERT_EQ(system_action->dispatch(action, "test", 0), ACTION_COMPLETE);
    EXPECT_FALSE(test_power_preferences["req_ap"]);
    EXPECT_EQ(restart_calls, 0U);
    ASSERT_NE(queued_job, nullptr);

    bool ok = false;
    queued_job(nullptr, &ok, nullptr, 0);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(test_power_preferences["req_ap"]);
    EXPECT_EQ(restart_calls, 1U);
    EXPECT_FALSE(ota_activity_try_begin());
    ota_activity_finish_network_transition();

    enqueue_result = LOOP_BRIDGE_BUSY;
    EXPECT_EQ(system_action->dispatch(action, "test", 0), ACTION_FAILED);
    enqueue_result = LOOP_BRIDGE_OK;

    ASSERT_TRUE(ota_activity_try_begin());
    ASSERT_EQ(system_action->dispatch(action, "test", 0), ACTION_COMPLETE);
    ok = true;
    queued_job(nullptr, &ok, nullptr, 0);
    EXPECT_FALSE(ok);
    EXPECT_EQ(restart_calls, 1U);
    ota_activity_finish();
}
