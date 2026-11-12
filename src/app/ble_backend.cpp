#include "ble_backend.h"

#if HAS_BLE || HAS_BLE_HID
#include "log_manager.h"
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

bool ble_backend_prepare(const char* device_name) {
    if (ble_backend_initialized()) return true;
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
    return initialized;
}
#endif
