#include "../version.h"
#include "board_config.h"
#include "config_manager.h"
#include "web_portal.h"
#include "log_manager.h"
#include "mqtt_manager.h"
#include "device_telemetry.h"
#include "sensors/sensor_manager.h"
#include "ble_advertiser.h"
#include "power_config.h"
#include "power_manager.h"
#include "portal_idle.h"
#include "wifi_manager.h"
#include "duty_cycle.h"
#if HEALTH_HISTORY_ENABLED
#include "health_history.h"
#endif
#include <WiFi.h>

#if HAS_DISPLAY
#include "display_manager.h"
#include "screen_saver_manager.h"
#endif

#if HAS_TOUCH
#include "touch_manager.h"
#endif

// Configuration
DeviceConfig device_config;
bool config_loaded = false;

#if HAS_MQTT
MqttManager mqtt_manager;
#endif

// Heartbeat interval
const unsigned long HEARTBEAT_INTERVAL_MS = 60000; // 60 seconds
unsigned long last_heartbeat_ms = 0;


// WiFi event handlers for connection lifecycle monitoring
void onWiFiConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
	LOGI("WiFi", "Connected to AP - waiting for IP");
}

void onWiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
	LOGI("WiFi", "Got IP: %s", WiFi.localIP().toString().c_str());
}

void onWiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
	uint8_t reason = info.wifi_sta_disconnected.reason;
	LOGI("WiFi", "Disconnected - reason: %d", reason);

	// Common disconnect reasons:
	// 2 = AUTH_EXPIRE, 3 = AUTH_LEAVE, 4 = ASSOC_EXPIRE
	// 8 = ASSOC_LEAVE, 15 = 4WAY_HANDSHAKE_TIMEOUT
	// 201 = NO_AP_FOUND, 202 = AUTH_FAIL, 205 = HANDSHAKE_TIMEOUT
}

static bool check_config_mode_button() {
	#if HAS_BUTTON
	pinMode(BUTTON_PIN, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);

	const unsigned long start = millis();
	while (millis() - start < 1500) {
		const bool pressed = (digitalRead(BUTTON_PIN) == (BUTTON_ACTIVE_LOW ? LOW : HIGH));
		if (!pressed) return false;
		delay(10);
	}

	LOGI("Power", "Config button held - entering Config Mode");
	return true;
	#else
	return false;
	#endif
}


void setup()
{
	// Optional device-side history for sparklines (/api/health/history)
	// Start as early as possible after a device boot.
	#if HEALTH_HISTORY_ENABLED
	health_history_start();
	#endif

	power_manager_boot_init();

	// Initialize logger (wraps Serial for web streaming)
	log_init(115200);
	if (power_manager_is_deep_sleep_wake()) {
		delay(10);
	} else {
		delay(1000);
	}

	// Register WiFi event handlers for connection lifecycle
	WiFi.onEvent(onWiFiConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
	WiFi.onEvent(onWiFiGotIP, ARDUINO_EVENT_WIFI_STA_GOT_IP);
	WiFi.onEvent(onWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

	LOGI("SYS", "Boot");
	LOGI("SYS", "Firmware: v%s", FIRMWARE_VERSION);
	LOGI("SYS", "Chip: %s (Rev %d)", ESP.getChipModel(), ESP.getChipRevision());
	LOGI("SYS", "CPU: %d MHz", ESP.getCpuFreqMHz());
	LOGI("SYS", "Flash: %d MB", ESP.getFlashChipSize() / (1024 * 1024));
	// On ESP32-P4 WiFi.macAddress() triggers ESP-Hosted SDIO init which may not
	// be ready yet; the MAC is logged later upon successful WiFi connection.
	#ifndef CONFIG_IDF_TARGET_ESP32P4
	LOGI("SYS", "MAC: %s", WiFi.macAddress().c_str());
	#endif
	#if HAS_BUILTIN_LED
	LOGI("SYS", "LED: GPIO%d (active %s)", LED_PIN, LED_ACTIVE_HIGH ? "HIGH" : "LOW");
	#endif
	// Example: Call board-specific function if available
	// #ifdef HAS_CUSTOM_IDENTIFIER
	// LOGI("SYS", "Board: %s", board_get_custom_identifier());
	// #endif

	// Baseline memory snapshot as early as possible.
	device_telemetry_log_memory_snapshot("boot");

	// Initialize device_config with sensible defaults
	// (Important: must happen before display_manager_init uses the config)
	memset(&device_config, 0, sizeof(DeviceConfig));
	device_config.backlight_brightness = 100;  // Default to full brightness
	device_config.mqtt_port = 0;

	#if HAS_DISPLAY
	// Screen saver defaults (v1)
	device_config.screen_saver_enabled = false;
	device_config.screen_saver_timeout_seconds = 300;
	device_config.screen_saver_fade_out_ms = 800;
	device_config.screen_saver_fade_in_ms = 400;
	#if HAS_TOUCH
	device_config.screen_saver_wake_on_touch = true;
	#else
	device_config.screen_saver_wake_on_touch = false;
	#endif
	#endif

	// Initialize board-specific hardware
	#if HAS_BUILTIN_LED
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, LED_ACTIVE_HIGH ? LOW : HIGH); // LED off initially
	#endif

	#if HAS_DISPLAY
	display_manager_init(&device_config);
	display_manager_set_splash_status("Loading config...");
	#endif

	#if HAS_TOUCH
	// Initialize touch after display is ready
	touch_manager_init();
	#endif

	// Initialize configuration manager
	#if HAS_DISPLAY
	display_manager_set_splash_status("Init NVS...");
	#endif
	config_manager_init();

	// Cache flash/sketch metadata early to avoid concurrent access from different tasks later
	// (e.g., MQTT publish + web API calls).
	device_telemetry_init();

	#if DEVICE_TELEMETRY_BACKGROUND_TASKS
	// Start CPU monitoring background task
	device_telemetry_start_cpu_monitoring();

	// Start 200ms health-window sampling (min/max fields between /api/health polls)
	device_telemetry_start_health_window_sampling();
	#endif

	// Try to load saved configuration
	#if HAS_DISPLAY
	display_manager_set_splash_status("Reading config...");
	#endif
	config_loaded = config_manager_load(&device_config);

	if (!config_loaded) {
		// No config found - set default device name
		String default_name = config_manager_get_default_device_name();
		strlcpy(device_config.device_name, default_name.c_str(), CONFIG_DEVICE_NAME_MAX_LEN);
		device_config.magic = CONFIG_MAGIC;
	}

	const bool force_config_mode_burst = power_manager_should_force_config_mode();
	if (force_config_mode_burst) {
		LOGI("Power", "Reset burst detected - entering Config Mode");
	}

	const bool force_config_mode_button = check_config_mode_button();
	const bool force_config_mode = force_config_mode_burst || force_config_mode_button;
	power_manager_configure(&device_config, config_loaded, force_config_mode);
	PowerMode boot_mode = power_manager_get_boot_mode();
	power_manager_set_current_mode(boot_mode);
	power_manager_led_set_mode(boot_mode);

	if (boot_mode == PowerMode::DutyCycle) {
		// Initialize sensors (optional adapters)
		sensor_manager_init();

		#if HAS_MQTT
		// Initialize MQTT manager (will only connect/publish when configured)
		char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
		config_manager_sanitize_device_name(device_config.device_name, sanitized, sizeof(sanitized));
		mqtt_manager.begin(&device_config, device_config.device_name, sanitized);
		#endif

		duty_cycle_run(&device_config);
		return;
	}

	// Re-apply brightness from loaded config (display was initialized before config load)
	#if HAS_DISPLAY && HAS_BACKLIGHT
	LOGI("Main", "Applying loaded brightness: %d%%", device_config.backlight_brightness);
	display_manager_set_backlight_brightness(device_config.backlight_brightness);
	#endif

	#if HAS_DISPLAY
	// Initialize screen saver manager after config is loaded.
	screen_saver_manager_init(&device_config);
	#endif

	const PublishTransport transport = power_config_parse_publish_transport(&device_config);
	const bool ble_only_always_on = (boot_mode == PowerMode::AlwaysOn) && (transport == PublishTransport::Ble) && (strlen(device_config.wifi_ssid) == 0);

	// Start WiFi BEFORE initializing web server (critical for ESP32-C3)
	#if HAS_DISPLAY
	display_manager_set_splash_status("Connecting WiFi...");
	#endif

	if (ble_only_always_on) {
		LOGW("Main", "BLE-only mode active (no WiFi SSID); portal unavailable unless forced into Config Mode");
	} else {
		if (boot_mode == PowerMode::Ap) {
			LOGI("Main", "AP mode selected - starting AP mode");
			web_portal_start_ap();
		} else if (!config_loaded) {
			LOGI("Main", "No config - starting AP mode");
			power_manager_set_current_mode(PowerMode::Ap);
			power_manager_led_set_mode(PowerMode::Ap);
			web_portal_start_ap();
		} else {
			LOGI("Main", "Config loaded - connecting to WiFi");
			if (wifi_manager_connect(&device_config, false)) {
				power_manager_note_wifi_success();
				wifi_manager_start_mdns(&device_config);
			} else {
				LOGW("Main", "WiFi failed - fallback to AP");
				power_manager_set_current_mode(PowerMode::Ap);
				power_manager_led_set_mode(PowerMode::Ap);
				web_portal_start_ap();
			}
		}

		// Initialize web portal AFTER WiFi is started
		web_portal_init(&device_config);

		portal_idle_init();
		portal_idle_set_timeout_seconds(device_config.portal_idle_timeout_seconds);
		portal_idle_set_mode(power_manager_get_current_mode());
	}

	// Initialize sensors (optional adapters)
	sensor_manager_init();

	#if HAS_MQTT
	// Initialize MQTT manager (will only connect/publish when configured)
	char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
	config_manager_sanitize_device_name(device_config.device_name, sanitized, sizeof(sanitized));
	mqtt_manager.begin(&device_config, device_config.device_name, sanitized);
	#endif

	last_heartbeat_ms = millis();
	LOGI("Main", "Setup complete");

	// Snapshot after all subsystems are initialized.
	device_telemetry_log_memory_snapshot("setup");

	#if HAS_DISPLAY
	// Show splash for minimum duration to ensure visibility
	display_manager_set_splash_status("Ready!");
	delay(2000);  // 2 seconds to see splash + status updates

	// Navigate to info screen
	display_manager_show_info();

	// Start the screen saver inactivity timer after the first runtime screen is visible.
	// This avoids counting boot + splash time as "inactivity".
	screen_saver_manager_notify_activity(false);
	#endif
}

void loop()
{
	power_manager_led_loop();
	power_manager_loop();

	#if HAS_DISPLAY
	screen_saver_manager_loop();
	#endif

	#if HAS_TOUCH
	touch_manager_loop();
	#endif

	// Handle web portal (DNS for captive portal)
	web_portal_handle();

	#if HAS_MQTT
	mqtt_manager.loop();
	#endif

	// Allow sensors to flush ISR-deferred work (e.g., instant MQTT publishes).
	sensor_manager_loop();

	#if HAS_BLE
	const bool allow_ble_always_on = config_loaded && power_manager_get_current_mode() != PowerMode::DutyCycle;
	ble_advertiser_loop(&device_config, allow_ble_always_on);
	#endif


	#if DEVICE_TELEMETRY_BACKGROUND_TASKS
	// Lightweight telemetry tripwires (runs from main loop only).
	device_telemetry_check_tripwires();
	#endif

	unsigned long current_ms = millis();

	// WiFi watchdog - monitor connection and reconnect if needed
	// Only run if we're not in AP mode (AP mode is the fallback, should stay active)
	wifi_manager_watchdog(&device_config, config_loaded, web_portal_is_ap_mode());

	// Check if it's time for heartbeat
	// TODO: consider moving heartbeat logging into device_telemetry.
	if (current_ms - last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS) {
		const DeviceMemorySnapshot mem = device_telemetry_get_memory_snapshot();
		if (WiFi.status() == WL_CONNECTED) {
			LOGI("Heartbeat", "Up:%ds heap=%u min=%u int=%u min=%u psram=%u | WiFi:%s (%s)",
				current_ms / 1000,
				(unsigned)mem.heap_free_bytes,
				(unsigned)mem.heap_min_free_bytes,
				(unsigned)mem.heap_internal_free_bytes,
				(unsigned)mem.heap_internal_min_free_bytes,
				(unsigned)mem.psram_free_bytes,
				WiFi.localIP().toString().c_str(),
				WiFi.getHostname());
		} else {
			LOGI("Heartbeat", "Up:%ds heap=%u min=%u int=%u min=%u psram=%u | WiFi: Disconnected",
				current_ms / 1000,
				(unsigned)mem.heap_free_bytes,
				(unsigned)mem.heap_min_free_bytes,
				(unsigned)mem.heap_internal_free_bytes,
				(unsigned)mem.heap_internal_min_free_bytes,
				(unsigned)mem.psram_free_bytes);
		}

		last_heartbeat_ms = current_ms;
	}

	delay(10);
}
