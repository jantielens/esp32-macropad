#pragma once

#include "board_config.h"
#include "ble_backend_selection.h"
#include <stdint.h>

#if HAS_BLE || HAS_BLE_HID
// Main-task lifecycle only. Preparing the shared host never starts advertising.
bool ble_backend_initialized();
bool ble_backend_power_supported();
bool ble_backend_prepare(const char* device_name);
// Boot configuration only; does not initialize the host.
bool ble_backend_configure_power(int8_t dbm);
int8_t ble_backend_tx_power_dbm();
#endif

inline bool bluetooth_power_valid(int power) {
    return power >= -12 && power <= 9 && power % 3 == 0;
}
