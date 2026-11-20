#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <string.h>

struct DeviceConfig;

// Reconnect tier returned by wifi_reconnect_get_tier().
enum class WifiReconnectTier : uint8_t {
    Tier1 = 1,  // SDK auto-reconnect window
    Tier2 = 2,  // Active reconnect with exponential backoff
    Tier3 = 3,  // Hard WiFi stack reset
};

enum class WifiRecoveryAction : uint8_t {
    None,
    Begin,
    ResetRadio,
    Reboot,
};

struct WifiRecoveryPolicy {
    unsigned long grace_ms;
    unsigned long reset_after_ms;
    unsigned long reboot_after_ms;
    unsigned long backoff_base_ms;
    unsigned long backoff_max_ms;
};

struct WifiConnectionDiagnostics {
    int16_t rssi;
    uint8_t channel;
    uint8_t bssid[6];
    uint8_t disconnect_reason;
    unsigned int retry_count;
    unsigned long associated_at_ms;
    unsigned long last_recovery_duration_ms;
    bool associated;
    bool has_disconnect_reason;
};

enum class WifiNetworkCommand : uint8_t { None, Connect, Disconnect, EnableAp, DisableAp, TurnOff };
enum class WifiNetworkState : uint8_t {
    Off, Starting, Connecting, Connected, Reconnecting, Ap, Stopping, Error
};

inline WifiNetworkCommand wifi_network_command_from_string(const char* command) {
    if (!command) return WifiNetworkCommand::None;
    if (!strcmp(command, "wifi_connect")) return WifiNetworkCommand::Connect;
    if (!strcmp(command, "wifi_disconnect")) return WifiNetworkCommand::Disconnect;
    if (!strcmp(command, "wifi_off")) return WifiNetworkCommand::TurnOff;
    if (!strcmp(command, "ap_enable")) return WifiNetworkCommand::EnableAp;
    if (!strcmp(command, "ap_disable")) return WifiNetworkCommand::DisableAp;
    return WifiNetworkCommand::None;
}

struct WifiNetworkSnapshot {
    WifiNetworkState state;
    bool ap_active;
    bool recovery_ap;
    bool station_enabled;
    char ssid[33];
    char ip[16];
    char hostname[32];
    char last_error[64];
};

// Start WiFi hardware early (SDIO link on P4, STA mode on others).
// Call before wifi_manager_connect() to overlap hardware bring-up with other init.
void wifi_manager_early_init();

// timeout_ms=0 retains the normal board-specific retry policy.
bool wifi_manager_connect(const DeviceConfig *config, bool allow_cached_bssid,
                          uint32_t timeout_ms = 0);
void wifi_manager_start_mdns(const DeviceConfig *config);
// Copy the successfully registered mDNS hostname, without ".local".
// Thread-safe; writes an empty string while registration is unavailable.
void wifi_manager_get_mdns_hostname(char* hostname, size_t size);
void wifi_manager_watchdog(const DeviceConfig *config, bool config_loaded, bool is_ap_mode);

// Register WiFi event handlers for event-driven reconnection.
// Call once after the first successful WiFi connection.
void wifi_manager_register_events();

// Admit one request from any task; duplicates are harmless, conflicts fail.
// Acceptance does not mean association or AP startup has completed.
bool wifi_manager_request_network_command(WifiNetworkCommand command);
bool wifi_manager_network_transition_pending();
void wifi_manager_get_network_snapshot(WifiNetworkSnapshot* snapshot);
// Call once after loading boot configuration, before starting portal tasks.
// The hostname remains this boot's configured value until restart.
void wifi_manager_init_network_snapshot(const DeviceConfig& config);
// Keep Wi-Fi off until an explicit Connect or Enable AP request.
void wifi_manager_enter_offline();

// Pure configuration validation helper.
bool wifi_ap_password_is_valid(const char* password);

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

// Determine the next recovery command. Manual requests bypass the grace period.
WifiRecoveryAction wifi_reconnect_next_action(unsigned long outage_ms,
                                              unsigned long since_last_attempt_ms,
                                              unsigned int retry_count,
                                              bool manual_request,
                                              const WifiRecoveryPolicy& policy,
                                              bool allow_reboot = true);

// Copy cached association and recovery diagnostics. This does not query WiFi.
void wifi_manager_get_diagnostics(WifiConnectionDiagnostics* diagnostics);

#endif // WIFI_MANAGER_H
