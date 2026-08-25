#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "factory_reset_overrides/board_config.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <nvs_flash.h>

#define DEVICE_CLASS_H
#define CLASS_BRANDING_H
#define POWER_CONFIG_H
#define WEB_ASSETS_H
#define LOG_MANAGER_H

class Preferences;
struct DeviceConfig;
enum class PowerMode { AlwaysOn, DutyCycle, DutyCycleBle, DutyCycleEpaper, Config, Ap };
enum class MqttPublishScope { SensorsOnly };

const char* device_class_get_full_name() { return "ESP32 Macropad"; }
void device_class_dispatch_config_defaults(DeviceConfig*) {}
void device_class_dispatch_config_load(DeviceConfig*, Preferences&) {}
void device_class_dispatch_config_save(const DeviceConfig*, Preferences&) {}
PowerMode power_config_parse_power_mode(const DeviceConfig*) { return PowerMode::AlwaysOn; }
MqttPublishScope power_config_parse_mqtt_publish_scope(const DeviceConfig*) { return MqttPublishScope::SensorsOnly; }
const char* power_config_power_mode_to_string(PowerMode) { return "always_on"; }
const char* power_config_mqtt_scope_to_string(MqttPublishScope) { return "sensors_only"; }

void log_write(...) {}
#define LOGE(...) ((void)0)
#define LOGW(...) ((void)0)
#define LOGI(...) ((void)0)

EspTestDouble ESP;
FakeStorage SD_MMC;
static esp_err_t s_nvs_erase_result = ESP_OK;

struct SerialTestDouble {
    void flush() {}
};

SerialTestDouble Serial;

unsigned long millis() { return 0; }
void delay(unsigned long) {}
esp_err_t nvs_flash_erase() { return s_nvs_erase_result; }
esp_err_t nvs_flash_init() { return ESP_OK; }

#include "config_manager.cpp"

static const char* const kOwnedRoots[] = {
    "/camera", "/config", "/icons", "/sounds", "/storage", "/prints", "/brews",
};

static void seed_filesystem() {
    SD_MMC.clear();
    for (const char* root : kOwnedRoots) {
        const std::string base(root);
        SD_MMC.add_directory(base);
        SD_MMC.add_file(base + "/top-level");
        SD_MMC.add_directory(base + "/nested");
        SD_MMC.add_file(base + "/nested/deep-file");
    }
    SD_MMC.add_directory("/user-data");
    SD_MMC.add_file("/user-data/keep");
    SD_MMC.add_file("/unrelated-sentinel");
    s_nvs_erase_result = ESP_OK;
}

static void assert_owned_roots_removed() {
    for (const char* root : kOwnedRoots) assert(!SD_MMC.exists(root));
}

static void assert_sentinels_preserved() {
    assert(SD_MMC.exists("/user-data"));
    assert(SD_MMC.exists("/user-data/keep"));
    assert(SD_MMC.exists("/unrelated-sentinel"));
}

static void test_success_removes_owned_roots_only() {
    seed_filesystem();
    assert(config_manager_factory_reset());
    assert_owned_roots_removed();
    assert_sentinels_preserved();
}

static void test_remove_failure_fails_reset() {
    seed_filesystem();
    SD_MMC.fail_remove_path = "/icons/nested/deep-file";
    assert(!config_manager_factory_reset());
    assert(SD_MMC.exists("/icons/nested/deep-file"));
    assert_sentinels_preserved();
}

static void test_rmdir_failure_fails_reset() {
    seed_filesystem();
    SD_MMC.fail_rmdir_path = "/sounds/nested";
    assert(!config_manager_factory_reset());
    assert(SD_MMC.exists("/sounds/nested"));
    assert_sentinels_preserved();
}

int main() {
    test_success_removes_owned_roots_only();
    test_remove_failure_fails_reset();
    test_rmdir_failure_fails_reset();
    std::puts("factory_reset_storage: PASS");
    return 0;
}