#include "ble_backend.h"

#if HAS_BLE || HAS_BLE_HID
#include "log_manager.h"
#include "reboot_service.h"
static constexpr uint8_t kBackendLogLevel = LOG_LEVEL;
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
#include <NimBLEDevice.h>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#include <esp32-hal-bt.h>
// NimBLE-Arduino 2.5.0 predates the core's BLE memory-retention constructor.
#include <esp32-hal-alloc-ble-mem.h>
#endif
#else
#include <BLEDevice.h>
#endif
#undef LOG_LEVEL_ERROR
#undef LOG_LEVEL_WARN
#undef LOG_LEVEL_INFO
#undef LOG_LEVEL_DEBUG
#undef LOG_LEVEL
#define LOG_LEVEL kBackendLogLevel
#include "log_manager.h"

bool ble_backend_initialized() {
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
    return NimBLEDevice::isInitialized();
#else
    return BLEDevice::getInitialized();
#endif
}

bool ble_backend_power_supported() {
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO || !defined(ARDUINO) || SOC_BLE_SUPPORTED
    return true;
#else
    return false;
#endif
}

namespace {
int8_t boot_tx_power_dbm = 9;
bool power_applied = false;

#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO || !defined(ARDUINO) || SOC_BLE_SUPPORTED
bool power_level(int8_t dbm, int& level) {
    switch (dbm) {
        case -12: level = ESP_PWR_LVL_N12; break;
        case -9: level = ESP_PWR_LVL_N9; break;
        case -6: level = ESP_PWR_LVL_N6; break;
        case -3: level = ESP_PWR_LVL_N3; break;
        case 0: level = ESP_PWR_LVL_N0; break;
        case 3: level = ESP_PWR_LVL_P3; break;
        case 6: level = ESP_PWR_LVL_P6; break;
        case 9: level = ESP_PWR_LVL_P9; break;
        default: return false;
    }
    return true;
}
#endif

bool apply_power() {
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO || !defined(ARDUINO) || SOC_BLE_SUPPORTED
    int level;
    if (!power_level(boot_tx_power_dbm, level)) return false;
    const auto power = static_cast<esp_power_level_t>(level);
    const esp_ble_power_type_t types[] = {ESP_BLE_PWR_TYPE_DEFAULT, ESP_BLE_PWR_TYPE_ADV, ESP_BLE_PWR_TYPE_SCAN};
    for (const auto type : types) {
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
        if (!NimBLEDevice::setPowerLevel(power, type) || NimBLEDevice::getPowerLevel(type) != power) {
#elif !defined(ARDUINO) || SOC_BLE_SUPPORTED
        // Core's void wrapper cannot report a failed controller operation.
        if (esp_ble_tx_power_set(type, power) != ESP_OK || esp_ble_tx_power_get(type) != power) {
#endif
            LOGE("BLE", "Could not apply Bluetooth transmit power (type=%d)", int(type));
            return false;
        }
    }
    LOGI("BLE", "Bluetooth transmit power: %+d dBm", int(boot_tx_power_dbm));
    return true;
#else
    LOGE("BLE", "Transmit power is unavailable on hosted HCI");
    return false;
#endif
}
}

bool ble_backend_configure_power(int8_t dbm) {
    if (!bluetooth_power_valid(dbm) || ble_backend_initialized()) {
        LOGE("BLE", "Invalid or late Bluetooth power configuration; reboot required");
        return false;
    }
    boot_tx_power_dbm = dbm;
    power_applied = false;
    return true;
}

int8_t ble_backend_tx_power_dbm() {
    return boot_tx_power_dbm;
}

bool ble_backend_prepare(const char* device_name) {
    if (reboot_is_ota_mode()) {
        LOGW("BLE", "Bluetooth unavailable in OTA mode");
        return false;
    }
    if (ble_backend_initialized()) {
        if (!ble_backend_power_supported()) return true;
        if (!power_applied) power_applied = apply_power();
        return power_applied;
    }
    power_applied = false;
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
#ifdef ARDUINO
    LOGI("BLE", "Initializing NimBLE-Arduino: internal free=%u largest=%u released=%u",
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        unsigned(btMemReleased(BT_MODE_BLE)));
#endif
    NimBLEDevice::init(device_name);
#else
    BLEDevice::init(device_name);
#endif
    const bool initialized = ble_backend_initialized();
    if (!initialized) LOGE("BLE", "Host initialization failed");
    else LOGI("BLE", "Host initialized (backend=%d)", BLE_BACKEND);
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO && defined(ARDUINO)
    LOGI("BLE", "After BLE: internal free=%u largest=%u PSRAM free=%u",
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
#endif
    if (!initialized) return false;
    if (!ble_backend_power_supported()) {
        LOGI("BLE", "Bluetooth power controlled by hosted co-processor");
        return true;
    }
    power_applied = apply_power();
    return power_applied;
}
#endif
