#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include "board_config.h"
#include "power_config.h"

struct DeviceConfig;

// Boot-time initialization (captures wake reason).
void power_manager_boot_init();

// Configure boot mode based on config + optional force flag.
void power_manager_configure(const DeviceConfig *config, bool config_loaded, bool force_config_mode);

// Accessors
PowerMode power_manager_get_boot_mode();
PowerMode power_manager_get_current_mode();
void power_manager_set_current_mode(PowerMode mode);
bool power_manager_should_force_config_mode();

bool power_manager_is_deep_sleep_wake();
bool power_manager_should_publish_mqtt_discovery();

// WiFi backoff tracking (RTC retained)
void power_manager_note_wifi_success();
uint32_t power_manager_note_wifi_failure(uint32_t base_seconds, uint32_t max_seconds);
uint32_t power_manager_get_wifi_backoff_seconds();

// Sleep helper
void power_manager_sleep_for(uint32_t seconds);

#if HAS_EPAPER_WAKE_BUTTON
enum class EpaperButtonWakeAction : uint8_t {
		None = 0,
		Refresh = 1,
		Config = 2,
};

// Returns true when the most recent wake was caused by the e-paper wake button (ext0).
bool power_manager_is_button_wake();
// Returns the boot-time classification of the wake button press.
EpaperButtonWakeAction power_manager_get_button_wake_action();
// RTC-retained refresh counter for diagnostics.
uint32_t power_manager_increment_refresh_count();
uint32_t power_manager_get_refresh_count();
#endif

// Background housekeeping
void power_manager_loop();

// LED behavior by mode (no-op if HAS_BUILTIN_LED=false)
void power_manager_led_set_mode(PowerMode mode);
void power_manager_led_loop();

#endif // POWER_MANAGER_H
