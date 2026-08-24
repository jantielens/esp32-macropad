#include "version.h"
#include "board_config.h"
#include "config_manager.h"
#include "web_portal.h"
#include "log_manager.h"
#include "mqtt_manager.h"
#include "mqtt_sub_store.h"
#include "mqtt_screen.h"
#include "mqtt_wake.h"
#include "mqtt_audio.h"
#include "mqtt_notify.h"
#include "mqtt_triggers.h"
#include "device_telemetry.h"
#include "sensors/sensor_manager.h"
#include "power_config.h"
#include "power_manager.h"
#include "portal_idle.h"
#include "wifi_manager.h"
#include "device_class.h"
#include "duty_cycle.h"
#include "hw_buttons.h"
#include "hw_button_config.h"
#if HAS_DISPLAY || HAS_BUTTON
#include "action_dispatch.h"
#endif
#if HAS_BLE
#include "ble_telemetry.h"
#endif
#if HEALTH_HISTORY_ENABLED
#include "health_history.h"
#endif
#include <WiFi.h>

#if HAS_DISPLAY
#include "display_manager.h"
#include "expr_binding.h"
#include "health_binding.h"
#include "icon_store.h"
#include "pad_binding.h"
#include "sound_store.h"
#include "boot_actions.h"
#include "pad_block.h"
#include "list_provider.h"
#include "list_binding.h"
#include "net_binding.h"
#include "time_binding.h"
#include "timer_binding.h"
#include "music_binding.h"
#if HAS_AUDIO_INPUT && HAS_DISPLAY
#include "audio_input_binding.h"
#endif
#include "timer_config.h"
#include "pad_config.h"
#include "screen_saver_manager.h"
#include "message_bubble.h"
#include "visual_alert.h"
#include "swipe_config.h"
#include "button_defaults.h"
#endif

#if HAS_IMAGE_FETCH
#include "image_fetch.h"
#endif

#if HAS_BLE_HID
#include "ble_hid.h"
#endif

#if HAS_AUDIO
#include "audio.h"
#endif

#if HAS_AUDIO_INPUT
#include "audio_input.h"
#endif

#include "i2c_bus.h"
#include "sd_probe.h"
#include "sd_storage.h"
#include "storage.h"
#include "ota_activity.h"

#if HAS_NATIVE_EXTENSIONS
#include "native_extension.h"
#endif

#include <esp_ota_ops.h>
#include <esp_heap_caps.h>

#ifdef LOOP_TASK_STACK_SIZE
SET_LOOP_TASK_STACK_SIZE(LOOP_TASK_STACK_SIZE);
#endif

#if HAS_TOUCH
#include "touch_manager.h"
#endif

#if HAS_CAMERA
#include "camera.h"
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
	#if HAS_CONFIG_MODE_BUTTON
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

	// Register device classes before wake classification so each class can
	// participate in power_manager_boot_init()'s dispatch.
	extern void device_classes_register_all();
	device_classes_register_all();

	power_manager_boot_init();

	// Initialize logger (wraps Serial for web streaming)
	log_init(115200);
	if (power_manager_is_deep_sleep_wake()) {
		delay(10);
	} else {
		delay(100);
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

	#if SD_PROBE_ON_BOOT
	// Diagnostic-only: runs before display init so its serial output is the
	// first thing visible during new-board bring-up. No-op when the flag is off.
	sd_probe_run();
	#endif

	#if HAS_DISPLAY
	display_manager_init(&device_config);
	display_manager_set_splash_status("Loading config...");
	#endif

	#if USE_SD_STORAGE
	// Mount SD card now that the splash screen is up — on failure the halt
	// message below is visible to the user. There is no fallback storage.
	if (storage_boot_should_halt(sd_storage_mount())) {
		#if HAS_DISPLAY
		display_manager_set_splash_status("SD card required. Correct or replace it, then reboot.");
		#endif
		LOGE("SYS", "SD card required; startup cannot continue. Check the card, filesystem, wiring, and power, then reboot.");
		while (true) {
			delay(1000);
			yield();
		}
	}
	#endif

	// Start WiFi hardware after required storage is known to be available.
	// On ESP32-P4 this kicks off the SDIO link to the C6 co-processor (~2-5 s)
	// which can run in the background while touch, config, and pads initialize.
	wifi_manager_early_init();

	#if HAS_TOUCH || HAS_CAMERA
	// Initialize Wire bus mutex before touch, audio, and camera SCCB access.
	i2c_bus_init();
	#endif

	#if HAS_TOUCH
	// Initialize touch after display is ready
	touch_manager_init();
	#endif

	#if HAS_CAMERA
	camera_init();
	#endif

	// Initialize configuration manager
	#if HAS_DISPLAY
	display_manager_set_splash_status("Init NVS...");
	#endif
	config_manager_init();

	// Cache flash/sketch metadata early to avoid concurrent access from different tasks later
	// (e.g., MQTT publish + web API calls).
	device_telemetry_init();

	#if DEVICE_TELEMETRY_CPU_MONITOR
	device_telemetry_start_cpu_monitoring();
	#endif

	#if DEVICE_TELEMETRY_HEALTH_WINDOW
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

	#if HAS_CAMERA
	if (!camera_set_capture_settings({
		.jpeg_quality = device_config.camera_jpeg_quality,
		.output_width = device_config.camera_output_width,
		.output_height = device_config.camera_output_height,
		.exposure_lines = device_config.camera_exposure_lines,
		.white_balance_red_q8 = device_config.camera_white_balance_red_q8,
		.white_balance_blue_q8 = device_config.camera_white_balance_blue_q8,
	})) {
		device_config.camera_jpeg_quality = CAMERA_JPEG_QUALITY_DEFAULT;
		device_config.camera_output_width = CAMERA_OUTPUT_WIDTH_DEFAULT;
		device_config.camera_output_height = CAMERA_OUTPUT_HEIGHT_DEFAULT;
		device_config.camera_exposure_lines = CAMERA_EXPOSURE_LINES_DEFAULT;
		device_config.camera_white_balance_red_q8 = CAMERA_WHITE_BALANCE_Q8_DEFAULT;
		device_config.camera_white_balance_blue_q8 = CAMERA_WHITE_BALANCE_Q8_DEFAULT;
		camera_set_capture_settings({
			.jpeg_quality = device_config.camera_jpeg_quality,
			.output_width = device_config.camera_output_width,
			.output_height = device_config.camera_output_height,
			.exposure_lines = device_config.camera_exposure_lines,
			.white_balance_red_q8 = device_config.camera_white_balance_red_q8,
			.white_balance_blue_q8 = device_config.camera_white_balance_blue_q8,
		});
		LOGW("Camera", "Invalid saved camera settings; restored defaults");
	}
	#endif

	#if HAS_SOUND_PLAYER
	// The audio worker discovers Music files as soon as it starts, so mount the
	// selected Storage backend before initializing audio.
	storage_mount();
	#endif

	// Initialize audio subsystem (must be after touch_manager_init since they
	// share the I2C bus — Wire must already be started, and after config load
	// so device_config.audio_volume is available).
	#if HAS_AUDIO
	audio_init(device_config.audio_volume);
	#if HAS_AUDIO_INPUT
	audio_input_meter_init();
	#endif
	#endif

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

	// Let registered device classes hook in before any network / display init.
	device_class_dispatch_setup_early(&device_config, boot_mode);

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

	#if HAS_BLE
	if (boot_mode == PowerMode::DutyCycleBle) {
		// Initialize sensors (their update path buffers BLE telemetry values)
		sensor_manager_init();

		ble_telemetry_init(device_config.device_name);

		duty_cycle_run(&device_config);
		return;
	}
	#endif

	// Generic duty-cycle dispatch: route to any registered DeviceClass that
	// owns this boot mode (e.g. e-paper). The class runs its full pipeline
	// (splashes, WiFi, refresh, sleep) and we exit setup() afterward so the
	// rest of the always-on init never runs in duty-cycle modes.
	if (const DeviceClass *dc = device_class_find_by_mode(boot_mode)) {
		if (dc->run_duty_cycle) {
			dc->run_duty_cycle(&device_config);
			return;
		}
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

	#if HAS_DISPLAY
	// Mount LittleFS for pad config persistence (non-fatal if no storage partition)
	pad_config_init();

	#if HAS_NATIVE_EXTENSIONS
	// The extension package is optional; an absent or invalid package must not
	// prevent the core firmware from continuing to boot.
	native_extension_init();
	#endif

	// Load swipe gesture actions from LittleFS (uses same filesystem)
	swipe_config_init();

	// Load device-level pad and button defaults from LittleFS
	button_defaults_init();

	// Load boot actions from LittleFS
	boot_actions_init();

	// Register core building blocks (feature branches add their own via pad_block_register)
	pad_block_init();

	// Register built-in list providers
	void list_provider_pads_init();
	list_provider_pads_init();

	// Initialize icon store and preload icons for all pads
	icon_store_init();
	icon_store_preload_pad_pages();
	#endif

	#if HAS_SOUND_PLAYER
	// Initialize sound file store (creates /sounds/ directory)
	sound_store_init();
	#endif

	// Start WiFi BEFORE initializing web server (critical for ESP32-C3)
	#if HAS_DISPLAY
	display_manager_set_splash_status("Connecting WiFi...");
	#endif

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
				device_telemetry_cache_rssi();
				wifi_manager_register_events();
				#if HAS_DISPLAY
				time_binding_start_ntp();
				#endif
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

	// Let registered device classes finish setup after WiFi / AP / portal
	// are up (no-op until a class registers).
	device_class_dispatch_setup_late(&device_config, power_manager_get_current_mode());

	// In AP/captive-portal mode the device's only job is to collect WiFi
	// credentials, save them, and reboot into STA mode. Skip all heavy
	// subsystem inits (MQTT, MQTT subscription store, BLE HID, sensors,
	// HA discovery) — they're irrelevant without LAN/internet and on
	// small-RAM SoCs (ESP32-C3) the mqtt_sub_store calloc alone (~136 KB)
	// will OOM the boot when WiFi softAP + lwIP + AsyncTCP are already
	// resident.
	const bool in_ap_mode = web_portal_is_ap_mode();
	if (in_ap_mode) {
		LOGI("Main", "AP mode: skipping sensor/BLE/MQTT init (saves RAM for portal)");
	}

	// Initialize sensors (optional adapters)
	if (!in_ap_mode) {
		sensor_manager_init();
	}

	// BLE HID keyboard — guarded by ble_hid_init() which bails gracefully
	// (init_error = true) if the NimBLE stack fails to allocate.
	#if HAS_BLE_HID
	if (!in_ap_mode && device_config.ble_enabled) {
		ble_hid_init(device_config.device_name, false);
	} else if (!in_ap_mode) {
		LOGI("Main", "BLE Keyboard disabled (saves ~70 KB RAM)");
	}
	#endif

	#if HAS_MQTT
	if (!in_ap_mode) {
		// Initialize MQTT manager (will only connect/publish when configured)
		char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
		config_manager_sanitize_device_name(device_config.device_name, sanitized, sizeof(sanitized));
		mqtt_manager.begin(&device_config, device_config.device_name, sanitized);
		// mqtt_sub_store caches inbound topic payloads (~136 KB) so widget
		// bindings can read them from the LVGL task. Only useful with a
		// display; on headless boards (C3) the store is dead weight and
		// fragments out of internal RAM. The other init calls below are
		// already compile-time no-op stubs without HAS_DISPLAY / HAS_AUDIO,
		// so they cost nothing on C3 — keeping the runtime gate documents
		// intent and avoids running them when no broker is configured.
		if (mqtt_manager.enabled()) {
			#if HAS_DISPLAY
			mqtt_sub_store_init();
			#endif
			mqtt_screen_init();
			mqtt_wake_init(&device_config);
			mqtt_audio_init();
			mqtt_notify_init();
#if MQTT_TRIGGERS_ENABLED
			mqtt_triggers_init();
#endif
		} else {
			LOGI("Main", "MQTT not configured: skipping handler init");
		}
	}
	#endif

	#if HAS_DISPLAY
	health_binding_init();
	time_binding_init();
	expr_binding_init();
	pad_binding_init();
	timer_binding_init();
	music_binding_init();
	#if HAS_AUDIO_INPUT && HAS_DISPLAY
	audio_input_binding_init();
	#endif
	list_binding_init();
	net_binding_init();
	timer_config_init();
	#endif

	// Hardware button actions (GPIO buttons). No-op stubs when !HAS_BUTTON.
	// Initialized after WiFi/MQTT setup so dispatched actions can fire
	// immediately, and well after check_config_mode_button() has released the
	// shared GPIO from its boot-hold probe.
	hw_button_config_init();
	hw_buttons_init();

	last_heartbeat_ms = millis();
	LOGI("Main", "Setup complete");

	// Mark OTA partition as valid so the bootloader won't roll back on next reboot.
	// Safe no-op when not running from an OTA partition.
	esp_ota_mark_app_valid_cancel_rollback();

	// Snapshot after all subsystems are initialized.
	device_telemetry_log_memory_snapshot("setup");

	#if HAS_MQTT
	// Attempt blocking MQTT connect + HA discovery during boot.
	// Non-fatal: if broker is unreachable we simply move on.
	if (mqtt_manager.enabled() && WiFi.status() == WL_CONNECTED) {
		#if HAS_DISPLAY
		display_manager_set_splash_status("MQTT discovery...");
		#endif
		mqtt_manager.connectAndPublishDiscoveryBlocking(8000);
	}
	#endif

	#if HAS_DISPLAY
	// Brief status flash so user can see connection result
	if (WiFi.status() == WL_CONNECTED) {
		char ip_msg[48];
		snprintf(ip_msg, sizeof(ip_msg), "Connected - %s", WiFi.localIP().toString().c_str());
		display_manager_set_splash_status(ip_msg);
	} else if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
		char ip_msg[48];
		snprintf(ip_msg, sizeof(ip_msg), "AP Mode - %s", WiFi.softAPIP().toString().c_str());
		display_manager_set_splash_status(ip_msg);
	}

	delay(500);

	// Navigate to pad_0 if configured, otherwise info screen
	if (pad_config_exists(0)) {
		display_manager_show_screen("pad_0", nullptr);
	} else {
		display_manager_show_info();
	}

	// Dispatch boot actions after first screen navigation
	boot_actions_dispatch();

	// Start image fetching AFTER the splash is dismissed and the runtime screen is
	// visible.  This avoids concurrent WiFi pressure (HTTP downloads + MQTT +
	// ESP-Hosted RPC) during the critical early-boot window.
	#if HAS_IMAGE_FETCH
	image_fetch_init();
	#endif

	// Start the screen saver inactivity timer after the first runtime screen is visible.
	// This avoids counting boot + splash time as "inactivity".
	screen_saver_manager_notify_activity(false);
	#endif
}

void loop()
{
	power_manager_led_loop();
	power_manager_loop();
	device_class_dispatch_loop();

	#if HAS_DISPLAY
	screen_saver_manager_loop();
	#endif

	#if HAS_DISPLAY || HAS_BUTTON
	action_dispatch_loop();
	#endif

	#if HAS_DISPLAY
	message_bubble_loop();
	visual_alert_loop();
	#endif

	#if HAS_TOUCH
	touch_manager_loop();
	#endif

	#if HAS_BLE_HID
	if (device_config.ble_enabled) {
		ble_hid_loop();
	}
	#endif

	// Handle web portal (DNS for captive portal)
	web_portal_handle();

	#if HAS_NATIVE_EXTENSIONS
	native_extension_loop();
	#endif

	#if HAS_MQTT
	if (!ota_activity_is_active()) {
	mqtt_manager.loop();
	mqtt_screen_loop();
	mqtt_wake_loop();
	mqtt_audio_loop();
	mqtt_notify_loop();
#if MQTT_TRIGGERS_ENABLED
		mqtt_triggers_loop();
#endif
	}
	#endif

	// Allow sensors to flush ISR-deferred work (e.g., instant MQTT publishes).
	sensor_manager_loop();

	// Process hardware button debounce/hold + action dispatch (no-op when !HAS_BUTTON).
	hw_buttons_loop();



	unsigned long current_ms = millis();

	// WiFi watchdog - monitor connection and reconnect if needed
	// Only run if we're not in AP mode (AP mode is the fallback, should stay active)
	wifi_manager_watchdog(&device_config, config_loaded, web_portal_is_ap_mode());

	// Check if it's time for heartbeat
	if (current_ms - last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS) {
		// Lightweight heartbeat — direct heap_caps calls, no full memory snapshot.
		const size_t int_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
		if (WiFi.status() == WL_CONNECTED) {
			LOGI("Heartbeat", "Up:%ds int=%u psram=%u | WiFi:%s (%s)",
				current_ms / 1000,
				(unsigned)int_free,
				(unsigned)psram_free,
				WiFi.localIP().toString().c_str(),
				WiFi.getHostname());
		} else {
			LOGI("Heartbeat", "Up:%ds int=%u psram=%u | WiFi: Disconnected",
				current_ms / 1000,
				(unsigned)int_free,
				(unsigned)psram_free);
		}
		last_heartbeat_ms = current_ms;
	}

	delay(10);
}
