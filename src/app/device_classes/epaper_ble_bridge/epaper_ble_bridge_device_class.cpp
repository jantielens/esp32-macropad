#include "board_config.h"

#if IS_EPAPER_BLE_BRIDGE

#include "device_class.h"
#include "device_classes/epaper_ble_bridge/epaper_ble_bridge_config.h"
#include "device_classes/epaper_ble_bridge/epaper_ble_bridge_runtime.h"
#include "log_manager.h"

void epaper_ble_bridge_config_api_get(JsonObject &root);
const char *epaper_ble_bridge_config_api_validate(JsonObject &body);
void epaper_ble_bridge_config_api_set(JsonObject &body);

namespace {

void setup_late(DeviceConfig *, PowerMode) {
    epaper_ble_bridge_runtime_setup();
}

void config_defaults(DeviceConfig *) {
    epaper_ble_bridge_config_defaults();
}

void config_load(DeviceConfig *, Preferences &preferences) {
    epaper_ble_bridge_config_load(preferences);
}

void config_save(const DeviceConfig *, Preferences &preferences) {
    epaper_ble_bridge_config_save(preferences);
}

void config_get(const DeviceConfig *, JsonObject &root) {
    epaper_ble_bridge_config_api_get(root);
}

const char *config_validate(const DeviceConfig *, JsonObject &body) {
    return epaper_ble_bridge_config_api_validate(body);
}

void config_set(DeviceConfig *, JsonObject &body) {
    epaper_ble_bridge_config_api_set(body);
    epaper_ble_bridge_runtime_reload_config();
}

const DeviceClass kEpaperBleBridgeClass = {
    /* name */ "epaper_ble_bridge",
    /* owned_mode */ PowerMode::AlwaysOn,
    /* on_setup_early */ nullptr,
    /* on_setup_late */ setup_late,
    /* on_loop */ epaper_ble_bridge_runtime_loop,
    /* run_duty_cycle */ nullptr,
    /* on_wake_classify */ nullptr,
    /* on_sleep_prepare */ nullptr,
    /* config_defaults */ config_defaults,
    /* config_load */ config_load,
    /* config_save */ config_save,
    /* config_api_get */ config_get,
    /* config_api_set */ config_set,
    /* mqtt_on_discovery */ nullptr,
    /* mqtt_publish_state */ nullptr,
    /* pad_hold_scheme */ nullptr,
    /* pad_hold_acquire */ nullptr,
    /* pad_hold_release */ nullptr,
    /* config_api_validate */ config_validate,
};

}  // namespace

void epaper_ble_bridge_device_class_register() {
    if (!device_class_register(&kEpaperBleBridgeClass)) {
        LOGE("EpaperBleBridge", "Device class registration failed");
    }
}

#include "epaper_ble_bridge_config.cpp"
#include "epaper_ble_bridge_logic.cpp"
#include "epaper_ble_bridge_runtime.cpp"

#endif  // IS_EPAPER_BLE_BRIDGE