#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

struct DeviceConfig;

bool wifi_manager_connect(const DeviceConfig *config, bool allow_cached_bssid);
void wifi_manager_start_mdns(const DeviceConfig *config);
void wifi_manager_watchdog(const DeviceConfig *config, bool config_loaded, bool is_ap_mode);

#endif // WIFI_MANAGER_H
