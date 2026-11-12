#pragma once

#include "board_config.h"
#include "ble_backend_selection.h"

#if HAS_BLE || HAS_BLE_HID
// Main-task lifecycle only. Preparing the shared host never starts advertising.
bool ble_backend_initialized();
bool ble_backend_prepare(const char* device_name);
#endif
