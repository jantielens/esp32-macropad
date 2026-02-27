#ifndef BLE_ADVERTISER_H
#define BLE_ADVERTISER_H

#include "board_config.h"
#include <ArduinoJson.h>

#if HAS_BLE

struct DeviceConfig;

bool ble_advertiser_init();
bool ble_advertiser_advertise_bthome(const DeviceConfig *config, const JsonObject &sensors, bool use_light_sleep);
void ble_advertiser_loop(const DeviceConfig *config, bool allow_advertise);

#else

struct DeviceConfig;

inline bool ble_advertiser_init() { return false; }
inline bool ble_advertiser_advertise_bthome(const DeviceConfig *, const JsonObject &, bool) { return false; }
inline void ble_advertiser_loop(const DeviceConfig *, bool) {}

#endif // HAS_BLE

#endif // BLE_ADVERTISER_H
