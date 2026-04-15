// ============================================================================
// WiFi reconnect pure helper functions
// ============================================================================
// Separated from wifi_manager.cpp for host-native unit testing.
// These functions have no WiFi or ESP32 dependency.

#include "wifi_manager.h"

unsigned long wifi_reconnect_next_backoff(unsigned int attempt, unsigned long base_ms, unsigned long max_ms) {
unsigned long interval = base_ms;
for (unsigned int i = 0; i < attempt; i++) {
interval *= 2;
if (interval >= max_ms) return max_ms;
}
return interval;
}

WifiReconnectTier wifi_reconnect_get_tier(unsigned long elapsed_ms,
                                          unsigned long tier1_ms,
                                          unsigned long tier2_ms) {
const unsigned long tier2_end = tier1_ms + tier2_ms;
if (elapsed_ms < tier1_ms) return WifiReconnectTier::Tier1;
if (elapsed_ms < tier2_end) return WifiReconnectTier::Tier2;
return WifiReconnectTier::Tier3;
}

bool wifi_reconnect_should_reboot(unsigned long total_outage_ms, unsigned long threshold_ms) {
return total_outage_ms >= threshold_ms;
}
