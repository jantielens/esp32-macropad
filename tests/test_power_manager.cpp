#include "power_manager.h"
#include "device_class.h"
#include "ota_activity.h"
#include "action_registry.h"
#include "main_loop_bridge.h"
#include "reboot_service.h"
#include "portal_idle.h"

#include <Preferences.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <gtest/gtest.h>

static unsigned reboot_requests = 0;
static RebootOwnership last_reboot_ownership = REBOOT_NORMAL;
static const ActionTypeDef* system_action = nullptr;

void action_type_register(const ActionTypeDef* type) { system_action = type; }
void wifi_manager_request_reconnect() {}
#if HAS_DISPLAY
void screen_saver_manager_sleep_now() {}
void screen_saver_manager_wake() {}
#endif
void reboot_request(uint32_t grace_ms, RebootOwnership ownership) {
    EXPECT_EQ(grace_ms, 200U);
    ++reboot_requests;
    last_reboot_ownership = ownership;
}

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

static unsigned long now_ms = 1000;
unsigned long millis() { return now_ms; }
bool web_portal_ota_in_progress() { return ota_activity_is_active(); }
bool time_service_localtime(time_t epoch, struct tm* result) {
    return localtime_r(&epoch, result) != nullptr;
}

TEST(PowerManager, RecoveryAndRestartLifecycle) {
    test_power_preferences.clear();
    test_power_reset_reason = ESP_RST_POWERON;
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

TEST(PowerManager, DutyCycleRecoveryFollowsResetReason) {
    const struct {
        const char* name;
        PowerMode mode;
    } modes[] = {
        {"duty_cycle_mqtt", PowerMode::DutyCycle},
        {"duty_cycle_ble", PowerMode::DutyCycleBle},
    };
    for (const auto& mode : modes) {
        for (const auto reason : {ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW,
                                  ESP_RST_PANIC, ESP_RST_DEEPSLEEP}) {
            SCOPED_TRACE(mode.name);
            SCOPED_TRACE(static_cast<int>(reason));
            test_power_preferences.clear();
            test_power_reset_reason = reason;
            power_manager_boot_init();
            DeviceConfig config = {};
            std::strcpy(config.operating_mode, mode.name);
            power_manager_configure(&config, true, false);

            const bool recovery = !HAS_DISPLAY && reason != ESP_RST_DEEPSLEEP;
            EXPECT_EQ(power_manager_is_duty_cycle_recovery(), recovery);
            EXPECT_EQ(power_manager_get_boot_mode(), recovery ? PowerMode::Config : mode.mode);
            EXPECT_EQ(power_manager_get_current_mode(), power_manager_get_boot_mode());
            EXPECT_STREQ(config.operating_mode, mode.name);
        }
    }
}

TEST(PowerManager, DutyCycleRecoveryPreservesExplicitModes) {
    test_power_preferences.clear();
    test_power_reset_reason = ESP_RST_SW;
    power_manager_boot_init();
    DeviceConfig config = {};
    for (const auto name : {"always_on", "duty_cycle_epaper_frame"}) {
        std::strcpy(config.operating_mode, name);
        power_manager_configure(&config, true, false);
        EXPECT_EQ(power_manager_get_boot_mode(), power_config_parse_power_mode(&config));
        EXPECT_FALSE(power_manager_is_duty_cycle_recovery());
    }

    std::strcpy(config.operating_mode, "duty_cycle_ble");
    power_manager_configure(&config, false, false);
    EXPECT_EQ(power_manager_get_boot_mode(), PowerMode::Ap);
    EXPECT_FALSE(power_manager_is_duty_cycle_recovery());

    power_manager_configure(&config, true, true);
    EXPECT_EQ(power_manager_get_boot_mode(), PowerMode::Config);
    EXPECT_FALSE(power_manager_is_duty_cycle_recovery());

    test_power_preferences["req_ap"] = true;
    power_manager_boot_init();
    power_manager_configure(&config, true, false);
    EXPECT_TRUE(power_manager_should_force_setup_ap());
    EXPECT_FALSE(power_manager_is_duty_cycle_recovery());
    EXPECT_EQ(power_manager_get_boot_mode(), PowerMode::DutyCycleBle);
}

TEST(PortalIdle, RecoveryUsesDutyCadenceAndRespectsActivityAndUploads) {
    now_ms = 1000;
    test_power_slept = false;
    portal_idle_init();
    portal_idle_set_timeout_seconds(30, 90);
    portal_idle_set_mode(PowerMode::Config);
    portal_idle_set_config_upload_in_progress(false);

    now_ms += 29999;
    portal_idle_loop();
    EXPECT_FALSE(test_power_slept);
    portal_idle_notify_activity();
    now_ms += 29999;
    portal_idle_loop();
    EXPECT_FALSE(test_power_slept);

    now_ms += 1;
    ASSERT_TRUE(ota_activity_try_begin());
    portal_idle_loop();
    EXPECT_FALSE(test_power_slept);
    ota_activity_finish();
    portal_idle_set_config_upload_in_progress(true);
    portal_idle_loop();
    EXPECT_FALSE(test_power_slept);
    portal_idle_set_config_upload_in_progress(false);
    portal_idle_loop();
    EXPECT_FALSE(test_power_slept);
    EXPECT_FALSE(ota_activity_try_begin());
    EXPECT_FALSE(ota_activity_try_begin_config_write());
    portal_idle_loop();
    EXPECT_TRUE(test_power_slept);
    EXPECT_EQ(test_power_sleep_us, 90000000ULL);
    ota_activity_finish_network_transition();

    // Failed STA connection must retain the recovery cadence in AP mode.
    test_power_slept = false;
    portal_idle_set_mode(PowerMode::Ap);
    portal_idle_notify_activity();
    now_ms += 30000;
    portal_idle_loop();
    portal_idle_loop();
    EXPECT_TRUE(test_power_slept);
    EXPECT_EQ(test_power_sleep_us, 90000000ULL);
    ota_activity_finish_network_transition();
}

TEST(PortalIdle, ManualSleepIsOneShotAndReservesSafeTransition) {
    now_ms = 1000;
    test_power_slept = false;
    portal_idle_init();
    portal_idle_set_timeout_seconds(0, 90);
    portal_idle_set_mode(PowerMode::AlwaysOn);
    portal_idle_set_config_upload_in_progress(false);

    ASSERT_TRUE(ota_activity_try_begin());
    EXPECT_FALSE(portal_idle_request_sleep(5, 500));
    ota_activity_finish();
    ASSERT_TRUE(ota_activity_try_begin_config_write());
    EXPECT_FALSE(portal_idle_request_sleep(5, 500));
    ota_activity_finish_config_write();
    portal_idle_set_config_upload_in_progress(true);
    EXPECT_FALSE(portal_idle_request_sleep(5, 500));
    portal_idle_set_config_upload_in_progress(false);

    ASSERT_TRUE(portal_idle_request_sleep(5, 500));
    EXPECT_FALSE(portal_idle_request_sleep(10, 500));
    EXPECT_FALSE(ota_activity_try_begin());
    EXPECT_FALSE(ota_activity_try_begin_config_write());
    now_ms = 1499;
    portal_idle_loop();
    EXPECT_FALSE(test_power_slept);
    now_ms = 1500;
    portal_idle_loop();
    EXPECT_TRUE(test_power_slept);
    EXPECT_EQ(test_power_sleep_us, 5000000ULL);
    ota_activity_finish_network_transition();

    test_power_slept = false;
    portal_idle_set_timeout_seconds(30, 90);
    portal_idle_set_mode(PowerMode::Config);
    portal_idle_notify_activity();
    now_ms += 30000;
    portal_idle_loop();
    portal_idle_loop();
    EXPECT_TRUE(test_power_slept);
    EXPECT_EQ(test_power_sleep_us, 90000000ULL);
    ota_activity_finish_network_transition();
}

TEST(PortalIdle, DisabledTimeoutAndAlwaysOnStayAwake) {
    test_power_slept = false;
    portal_idle_init();
    portal_idle_set_timeout_seconds(0, 90);
    portal_idle_set_mode(PowerMode::Config);
    now_ms += 60000;
    portal_idle_loop();
    EXPECT_FALSE(test_power_slept);

    portal_idle_set_timeout_seconds(30, 30);
    portal_idle_set_mode(PowerMode::AlwaysOn);
    portal_idle_loop();
    EXPECT_FALSE(test_power_slept);
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
    EXPECT_EQ(reboot_requests, 0U);
    ASSERT_NE(queued_job, nullptr);

    bool ok = false;
    queued_job(nullptr, &ok, nullptr, 0);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(test_power_preferences["req_ap"]);
    EXPECT_EQ(reboot_requests, 1U);
    EXPECT_EQ(last_reboot_ownership, REBOOT_NETWORK_TRANSITION);
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
    EXPECT_EQ(reboot_requests, 1U);
    ota_activity_finish();
}
