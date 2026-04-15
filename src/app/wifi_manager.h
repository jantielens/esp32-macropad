#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

struct DeviceConfig;

// Reconnect tier returned by wifi_reconnect_get_tier().
enum class WifiReconnectTier : uint8_t {
    Tier1 = 1,  // SDK auto-reconnect window
    Tier2 = 2,  // Active reconnect with exponential backoff
    Tier3 = 3,  // Hard WiFi stack reset
};

// Start WiFi hardware early (SDIO link on P4, STA mode on others).
// Call before wifi_manager_connect() to overlap hardware bring-up with other init.
void wifi_manager_early_init();

bool wifi_manager_connect(const DeviceConfig *config, bool allow_cached_bssid);
void wifi_manager_start_mdns(const DeviceConfig *config);
void wifi_manager_watchdog(const DeviceConfig *config, bool config_loaded, bool is_ap_mode);

// Register WiFi event handlers for event-driven reconnection.
// Call once after the first successful WiFi connection.
void wifi_manager_register_events();

// --- Pure helper functions (unit-testable, no WiFi dependency) ---

// Compute next backoff interval using exponential growth.
// Returns min(base_ms * 2^attempt, max_ms).
unsigned long wifi_reconnect_next_backoff(unsigned int attempt, unsigned long base_ms, unsigned long max_ms);

// Determine the reconnect tier based on elapsed outage time.
WifiReconnectTier wifi_reconnect_get_tier(unsigned long elapsed_ms,
                                          unsigned long tier1_ms,
                                          unsigned long tier2_ms);

// Check whether the device should reboot based on total outage duration.
bool wifi_reconnect_should_reboot(unsigned long total_outage_ms, unsigned long threshold_ms);

#endif // WIFI_MANAGER_H
