#include "version.h"
#include "board_config.h"
#include "config_manager.h"
#include "time_service.h"
#include "alarm_manager.h"
#include "web_portal.h"
#include "log_manager.h"
#include "mqtt_manager.h"
#include "mqtt_sub_store.h"
#include "mqtt_screen.h"
#include "mqtt_wake.h"
#include "mqtt_audio.h"
#include "mqtt_camera.h"
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
#if HAS_BOOT_LOGO
#include "boot_logo_store.h"
#endif
#include "dma2d_arbiter.h"
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
#if HAS_REMOTE_LOG
#include <esp_system.h>
#include <esp_idf_version.h>
#if ESP_IDF_VERSION_MAJOR >= 5
#include <esp_app_desc.h>
#endif
#endif

#if HAS_DISPLAY
#include "binding_builtin_schemes.h"
#include "display_manager.h"
#include "icon_store.h"
#include "sound_store.h"
#include "boot_actions.h"
#include "pad_block.h"
#include "list_provider.h"
#include "time_binding.h"
#include "timer_config.h"
#include "pad_config.h"
#include "screen_saver_manager.h"
#include "device_dialog.h"
#include "visual_alert.h"
#include "swipe_config.h"
#include "button_defaults.h"
#endif

#if HAS_IMAGE_FETCH
#include "image_fetch.h"
#endif

#if HAS_IMAGE_LIBRARY
#include "image_library_config.h"
#include "image_library_runtime.h"
#include "local_image_loader.h"
#endif

#if HAS_BLE_HID
#include "ble_hid.h"
#endif

#if HAS_USB_HID
#include "usb_hid.h"
#include "mouse_hid.h"
#include "gamepad_hid.h"
#endif
#include "keyboard_hid.h"

#if HAS_AUDIO
#include "audio.h"
#endif

#if HAS_AUDIO_INPUT
#include "audio_input.h"
#endif

#include "i2c_bus.h"
#include "board_hardware.h"
#ifdef BOARD_HARDWARE_LOOP
#include BOARD_HARDWARE_HEADER
#endif
#include "sd_probe.h"
#include "sd_storage.h"
#include "storage.h"
#include "ota_activity.h"
#include "ha_live.h"

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
#include "camera_feed.h"
#include "camera_motion.h"
#include "camera_motion_actions.h"
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

static bool g_normal_runtime_started = false;
static bool g_boot_entered_setup_ap = false;
static bool g_setup_ap_active = false;

static void load_boot_logo() {
	#if HAS_DISPLAY && HAS_BOOT_LOGO
	if (device_config.boot_logo_slot != CONFIG_BOOT_LOGO_SLOT_NONE) {
		const uint32_t background = device_config.boot_appearance_background_color ==
				CONFIG_BOOT_COLOR_INHERIT
				? BOOT_APPEARANCE_DEFAULT_BACKGROUND_RGB
				: device_config.boot_appearance_background_color;
		if (!boot_logo_store_load(device_config.boot_logo_slot, background)) {
			LOGW("BootLogo", "Custom boot logo unavailable; continuing startup");
		}
	}
	#endif
}

static void start_normal_runtime(bool dispatch_boot_actions) {
	if (g_normal_runtime_started) return;
	g_normal_runtime_started = true;

	#if HAS_DISPLAY && HAS_BACKLIGHT
	display_manager_set_backlight_brightness(device_config.backlight_brightness);
	#endif

	#if HAS_DISPLAY
	screen_saver_manager_init(&device_config);
	pad_config_init();
	#if HAS_NATIVE_EXTENSIONS
	native_extension_init();
	#endif
	swipe_config_init();
	button_defaults_init();
	if (displayManager) {
		const ButtonDefaults* defaults = button_defaults_get();
		displayManager->initTypography(defaults->default_font, defaults->base_text_size);
	}
	boot_actions_init();
	pad_block_init();
	void list_provider_pads_init();
	list_provider_pads_init();
	icon_store_init();
	icon_store_preload_pad_pages();
	#elif HAS_STORAGE_BROWSER
	storage_mount();
	#endif

	#if CAMERA_MOTION_ACTIONS_ENABLED
	camera_motion_actions_init();
	#endif
	#if HAS_SOUND_PLAYER
	sound_store_init();
	#endif

	sensor_manager_init();

	#if HAS_BLE_HID
	if (keyboard_hid_transport() == KeyboardTransport::Ble) {
		ble_hid_init(device_config.device_name, false);
	} else {
		LOGI("Main", "BLE Keyboard disabled (saves ~70 KB RAM)");
	}
	#endif

	#if HAS_MQTT
	char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
	config_manager_sanitize_device_name(device_config.device_name, sanitized, sizeof(sanitized));
	mqtt_manager.begin(&device_config, device_config.device_name, sanitized);
	if (mqtt_manager.enabled()) {
		#if HAS_DISPLAY
		mqtt_sub_store_init();
		#endif
		mqtt_screen_init();
		mqtt_wake_init(&device_config);
		mqtt_audio_init();
		mqtt_camera_init();
		mqtt_notify_init();
		#if MQTT_TRIGGERS_ENABLED
		mqtt_triggers_init();
		#endif
	} else {
		LOGI("Main", "MQTT not configured: skipping handler init");
	}
	#endif

	#if HAS_DISPLAY
		#if HAS_IMAGE_LIBRARY
	image_library_config_init();
	image_library_runtime_init();
		#endif
	binding_builtin_schemes_init();
	timer_config_init();
	#endif
	#if ALARM_ENABLED
	alarm_manager_init();
	#endif

	hw_button_config_init();
	hw_buttons_init();
	device_class_dispatch_normal_runtime(&device_config);
	last_heartbeat_ms = millis();
	LOGI("Main", "Normal runtime initialized");

	esp_ota_mark_app_valid_cancel_rollback();
	device_telemetry_log_memory_snapshot("setup");

	#if HAS_HA_LIVE
	ha_live_init(device_config.ha_url, device_config.ha_token);
	#endif

	#if HAS_MQTT
	if (mqtt_manager.enabled() && WiFi.status() == WL_CONNECTED) {
		#if HAS_DISPLAY
		display_manager_set_splash_status("Connecting to MQTT...");
		#endif
		mqtt_manager.connectAndPublishDiscoveryBlocking(8000);
	}
	#endif

	#if HAS_DISPLAY
	if (WiFi.status() == WL_CONNECTED) {
		char ip_msg[48];
		snprintf(ip_msg, sizeof(ip_msg), "Wi-Fi connected: %s", WiFi.localIP().toString().c_str());
		display_manager_set_splash_status(ip_msg);
	} else {
		display_manager_set_splash_status("Offline mode");
	}
	delay(500);
	if (pad_config_exists(0)) {
		display_manager_show_screen("pad_0", nullptr);
	} else {
		display_manager_show_info();
	}
	if (dispatch_boot_actions) boot_actions_dispatch();
	#if HAS_IMAGE_FETCH
	image_fetch_init();
	#endif
	screen_saver_manager_notify_activity(false);
	#else
	(void)dispatch_boot_actions;
	#endif

	#if HAS_REMOTE_LOG
	remote_log_finish_boot();
	#endif
}

bool app_continue_offline_from_setup() {
	if (!g_boot_entered_setup_ap || g_normal_runtime_started) return false;
	if (!ota_activity_try_begin_network_transition()) {
		LOGW("WiFi", "Continue offline deferred: update or configuration write is active");
		return false;
	}

	web_portal_stop_ap();
	g_setup_ap_active = false;
	wifi_manager_enter_offline();
	power_manager_set_current_mode(PowerMode::AlwaysOn);
	power_manager_led_set_mode(PowerMode::AlwaysOn);
	portal_idle_set_mode(PowerMode::AlwaysOn);
	start_normal_runtime(false);
	ota_activity_finish_network_transition();
	return true;
}

void setup()
{
	#if HAS_PSRAM && MALLOC_PSRAM_THRESHOLD_BYTES > 0
	if (psramFound()) {
		heap_caps_malloc_extmem_enable(MALLOC_PSRAM_THRESHOLD_BYTES);
	}
	#endif

	dma2d_arbiter_init();

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
	#if HAS_REMOTE_LOG
	LOGI("SYS", "SDK: %s", esp_get_idf_version());
	const esp_reset_reason_t reset_reason = esp_reset_reason();
	const char* reset_name = "Unknown";
	switch (reset_reason) {
		case ESP_RST_POWERON: reset_name = "Power On"; break;
		case ESP_RST_SW: reset_name = "Software"; break;
		case ESP_RST_PANIC: reset_name = "Panic"; break;
		case ESP_RST_INT_WDT: reset_name = "Interrupt WDT"; break;
		case ESP_RST_TASK_WDT: reset_name = "Task WDT"; break;
		case ESP_RST_WDT: reset_name = "WDT"; break;
		case ESP_RST_DEEPSLEEP: reset_name = "Deep Sleep"; break;
		case ESP_RST_BROWNOUT: reset_name = "Brownout"; break;
		case ESP_RST_SDIO: reset_name = "SDIO"; break;
		#if ESP_IDF_VERSION_MAJOR >= 5
		case ESP_RST_USB: reset_name = "USB"; break;
		case ESP_RST_JTAG: reset_name = "JTAG"; break;
		case ESP_RST_EFUSE: reset_name = "eFuse"; break;
		case ESP_RST_PWR_GLITCH: reset_name = "Power Glitch"; break;
		case ESP_RST_CPU_LOCKUP: reset_name = "CPU Lockup"; break;
		#endif
		default: break;
	}
	LOGI("SYS", "Reset reason: %s (%d)", reset_name, static_cast<int>(reset_reason));
	char elf_sha256[65];
	#if ESP_IDF_VERSION_MAJOR >= 5
	esp_app_get_elf_sha256(elf_sha256, sizeof(elf_sha256));
	#else
	esp_ota_get_app_elf_sha256(elf_sha256, sizeof(elf_sha256));
	#endif
	LOGI("SYS", "Firmware ELF SHA256: %s", elf_sha256);
	#endif
	LOGI("SYS", "Chip: %s (Rev %d)", ESP.getChipModel(), ESP.getChipRevision());
	LOGI("SYS", "CPU: %d MHz", ESP.getCpuFreqMHz());
	LOGI("SYS", "Flash: %d MB", ESP.getFlashChipSize() / (1024 * 1024));
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

	#if HAS_TOUCH || HAS_CAMERA || HAS_AUDIO || defined(BOARD_HARDWARE_SOURCE)
	i2c_bus_init();
	#endif
	if (!board_hardware_init()) {
		LOGE("Board", "Hardware initialization failed; dependent peripherals unavailable");
	}

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

	// Load persisted settings before creating the display. The Inkplate driver
	// samples its panel mode during initialization and never changes it live.
	config_manager_init();
	config_loaded = config_manager_load(&device_config);
	if (!device_config.timezone[0]) strlcpy(device_config.timezone, "UTC0", sizeof(device_config.timezone));
	time_service_set_timezone(device_config.timezone);
	if (!config_loaded) {
		String default_name = config_manager_get_default_device_name();
		strlcpy(device_config.device_name, default_name.c_str(), CONFIG_DEVICE_NAME_MAX_LEN);
		device_config.magic = CONFIG_MAGIC;
	}

	#if HAS_USB_HID
	usb_hid_init(device_config.device_name, device_config.keyboard_transport == KeyboardTransport::Usb);
	#endif
	#if HAS_BLE_HID || HAS_USB_HID
	keyboard_hid_init(device_config.keyboard_transport);
	#endif

	#if HAS_DISPLAY
	display_manager_init(&device_config);
	display_manager_set_splash_status("Initializing device...");
	#endif

	#if USE_SD_STORAGE
	// Mount SD card now that the splash screen is up — on failure the halt
	// message below is visible to the user. There is no fallback storage.
	if (storage_boot_should_halt(sd_storage_mount())) {
		#if HAS_DISPLAY
		display_manager_set_splash_status("SD card unavailable. Check or replace it, then restart.");
		#endif
		LOGE("SYS", "SD card required; startup cannot continue. Check the card, filesystem, wiring, and power, then reboot.");
		while (true) {
			delay(1000);
			yield();
		}
	}
	#endif

	// Load best-effort once the splash and required storage are ready.
	load_boot_logo();

	const bool force_config_mode_burst = power_manager_should_force_config_mode();
	if (force_config_mode_burst) {
		LOGI("Power", "Reset burst detected - entering Config Mode");
	}
	const bool force_config_mode_button = check_config_mode_button();
	const bool force_config_mode =
			force_config_mode_burst || force_config_mode_button;
	power_manager_configure(&device_config, config_loaded, force_config_mode);
	PowerMode boot_mode = power_manager_get_boot_mode();
	if (power_manager_should_force_setup_ap()) {
		boot_mode = PowerMode::Ap;
	}
	power_manager_set_current_mode(boot_mode);
	power_manager_led_set_mode(boot_mode);

	// Start WiFi hardware after required storage is known to be available.
	// On ESP32-P4 this kicks off the SDIO link to the C6 co-processor (~2-5 s)
	// which can run in the background while touch, config, and pads initialize.
	const bool defer_wifi_init =
			boot_mode == PowerMode::DutyCycleBle ||
			device_class_dispatch_defer_wifi_init(&device_config, boot_mode);
	if (!defer_wifi_init) {
		wifi_manager_early_init();
		#ifndef CONFIG_IDF_TARGET_ESP32P4
		LOGI("SYS", "MAC: %s", WiFi.macAddress().c_str());
		#endif
	} else {
		LOGI("WiFi", "Early init deferred for offline wake");
	}

	#if HAS_TOUCH
	// Initialize touch after display is ready
	touch_manager_init((DISPLAY_ROTATION + device_config.display_rotation) & 3);
	#endif

	#if HAS_CAMERA
	camera_init();
	camera_feed_init();
	#endif

	// Cache flash/sketch metadata early to avoid concurrent access from different tasks later
	// (e.g., MQTT publish + web API calls).
	device_telemetry_init();

	#if DEVICE_TELEMETRY_CPU_MONITOR
	device_telemetry_start_cpu_monitoring();
	#endif

	#if DEVICE_TELEMETRY_HEALTH_WINDOW
	device_telemetry_start_health_window_sampling();
	#endif

	#if HAS_CAMERA
	if (!camera_set_capture_settings({
		.jpeg_quality = device_config.camera_jpeg_quality,
		.feed_target_fps = device_config.camera_feed_target_fps,
		.rotation = device_config.camera_rotation,
		.output_width = device_config.camera_output_width,
		.output_height = device_config.camera_output_height,
		.exposure_lines = device_config.camera_exposure_lines,
		.white_balance_red_q8 = device_config.camera_white_balance_red_q8,
		.white_balance_blue_q8 = device_config.camera_white_balance_blue_q8,
	})) {
		device_config.camera_jpeg_quality = CAMERA_JPEG_QUALITY_DEFAULT;
		device_config.camera_feed_target_fps = CAMERA_FEED_TARGET_FPS_DEFAULT;
		device_config.camera_rotation = CAMERA_ROTATION_DEFAULT;
		device_config.camera_output_width = CAMERA_OUTPUT_WIDTH_DEFAULT;
		device_config.camera_output_height = CAMERA_OUTPUT_HEIGHT_DEFAULT;
		device_config.camera_exposure_lines = CAMERA_EXPOSURE_LINES_DEFAULT;
		device_config.camera_white_balance_red_q8 = CAMERA_WHITE_BALANCE_Q8_DEFAULT;
		device_config.camera_white_balance_blue_q8 = CAMERA_WHITE_BALANCE_Q8_DEFAULT;
		camera_set_capture_settings({
			.jpeg_quality = device_config.camera_jpeg_quality,
			.feed_target_fps = device_config.camera_feed_target_fps,
			.rotation = device_config.camera_rotation,
			.output_width = device_config.camera_output_width,
			.output_height = device_config.camera_output_height,
			.exposure_lines = device_config.camera_exposure_lines,
			.white_balance_red_q8 = device_config.camera_white_balance_red_q8,
			.white_balance_blue_q8 = device_config.camera_white_balance_blue_q8,
		});
		LOGW("Camera", "Invalid saved camera settings; restored defaults");
	}
	if (!camera_motion_set_settings({
		.enabled = device_config.camera_motion_enabled,
		.analyze_every_nth_frame = device_config.camera_motion_analyze_every_nth_frame,
		.sensitivity = device_config.camera_motion_sensitivity,
		.presence_hold_seconds = device_config.camera_presence_hold_seconds,
		#if HAS_DISPLAY
		.keep_display_awake = device_config.camera_motion_keep_display_awake,
		#endif
	})) {
		device_config.camera_motion_enabled = false;
		device_config.camera_motion_analyze_every_nth_frame = CAMERA_MOTION_ANALYZE_EVERY_DEFAULT;
		device_config.camera_motion_sensitivity = CAMERA_MOTION_SENSITIVITY_DEFAULT;
		device_config.camera_presence_hold_seconds = CAMERA_PRESENCE_HOLD_SECONDS_DEFAULT;
		camera_motion_set_settings({
			.enabled = false,
			.analyze_every_nth_frame = device_config.camera_motion_analyze_every_nth_frame,
			.sensitivity = device_config.camera_motion_sensitivity,
			.presence_hold_seconds = device_config.camera_presence_hold_seconds,
			#if HAS_DISPLAY
			.keep_display_awake = false,
			#endif
		});
		LOGW("Camera", "Invalid saved motion settings; restored defaults");
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
	audio_set_amplifier_policy(device_config.audio_idle_mute, device_config.audio_wake_delay_ms);
	audio_init(device_config.audio_volume);
	#if HAS_AUDIO_INPUT
	audio_input_meter_init();
	#endif
	#endif

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

		ble_telemetry_init(device_config.device_name, device_config.ble_tx_power_dbm);

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

	bool enter_setup_ap = (boot_mode == PowerMode::Ap);
	if (enter_setup_ap) {
		LOGI("Main", "Setup AP mode selected");
		#if HAS_DISPLAY
		display_manager_set_splash_status("Starting setup network...");
		#endif
		if (!web_portal_start_ap(&device_config)) {
			LOGE("Main", "Setup AP failed to start");
			#if HAS_DISPLAY
			display_manager_set_splash_status("Setup network failed");
			#endif
		}
	} else if (!config_loaded) {
		enter_setup_ap = true;
		power_manager_set_current_mode(PowerMode::Ap);
		power_manager_led_set_mode(PowerMode::Ap);
		#if HAS_DISPLAY
		display_manager_set_splash_status("Starting setup network...");
		#endif
		if (!web_portal_start_ap(&device_config)) {
			LOGE("Main", "Setup AP failed to start");
			#if HAS_DISPLAY
			display_manager_set_splash_status("Setup network failed");
			#endif
		}
	} else {
		LOGI("Main", "Connecting to saved Wi-Fi");
		#if HAS_DISPLAY
		display_manager_set_splash_status("Connecting to Wi-Fi...");
		#endif
		if (wifi_manager_connect(&device_config, false)) {
			power_manager_note_wifi_success();
			wifi_manager_start_mdns(&device_config);
			device_telemetry_cache_rssi();
			wifi_manager_register_events();
			#if HAS_DISPLAY
			time_binding_start_ntp();
			#endif
		} else if (boot_mode == PowerMode::AlwaysOn &&
				strcmp(device_config.wifi_failure_mode, "offline") == 0) {
			LOGW("Main", "Saved Wi-Fi failed; continuing offline");
			wifi_manager_enter_offline();
			power_manager_set_current_mode(PowerMode::AlwaysOn);
			power_manager_led_set_mode(PowerMode::AlwaysOn);
		} else {
			LOGW("Main", "Saved Wi-Fi failed; entering setup AP");
			enter_setup_ap = true;
			power_manager_set_current_mode(PowerMode::Ap);
			power_manager_led_set_mode(PowerMode::Ap);
			#if HAS_DISPLAY
			display_manager_set_splash_status("Starting setup network...");
			#endif
			if (!web_portal_start_ap(&device_config)) {
				LOGE("Main", "Setup AP failed to start");
				#if HAS_DISPLAY
				display_manager_set_splash_status("Setup network failed");
				#endif
			}
		}
	}

	g_boot_entered_setup_ap = enter_setup_ap;
	g_setup_ap_active = enter_setup_ap;
	web_portal_init(&device_config);
	portal_idle_init();
	portal_idle_set_timeout_seconds(device_config.portal_idle_timeout_seconds);
	portal_idle_set_mode(power_manager_get_current_mode());

	device_class_dispatch_setup_late(&device_config, power_manager_get_current_mode());

	if (g_boot_entered_setup_ap) {
		#if HAS_DISPLAY && !IS_EPAPER_FRAME
		if (displayManager) displayManager->showWifiSetup();
		#endif
		esp_ota_mark_app_valid_cancel_rollback();
		#if HAS_REMOTE_LOG
		remote_log_finish_boot();
		#endif
		return;
	}

	start_normal_runtime(true);
}

void loop()
{
#ifdef BOARD_HARDWARE_LOOP
	BOARD_HARDWARE_LOOP();
#endif
	time_service_loop();
	#if ALARM_ENABLED
	alarm_manager_loop();
	#endif
	device_telemetry_mark_main_loop(DEVICE_RUNTIME_PHASE_MAIN_HOUSEKEEPING);
	power_manager_led_loop();
	power_manager_loop();
	device_class_dispatch_loop();

	#if HAS_DISPLAY
	device_telemetry_mark_main_loop(DEVICE_RUNTIME_PHASE_MAIN_SCREEN_SAVER);
	screen_saver_manager_loop();
		#if HAS_IMAGE_LIBRARY
	image_library_runtime_tick(millis());
		#endif
	#endif

	#if HAS_DISPLAY || HAS_BUTTON
	device_telemetry_mark_main_loop(DEVICE_RUNTIME_PHASE_MAIN_ACTION_DISPATCH);
	action_dispatch_loop();
	#endif

	#if HAS_CAMERA
	device_telemetry_mark_main_loop(DEVICE_RUNTIME_PHASE_MAIN_CAMERA_FEED);
	camera_feed_loop();
	#endif

	#if HAS_DISPLAY
	device_telemetry_mark_main_loop(DEVICE_RUNTIME_PHASE_MAIN_DISPLAY_EFFECTS);
	visual_alert_loop();
	#endif

	#if HAS_TOUCH
	device_telemetry_mark_main_loop(DEVICE_RUNTIME_PHASE_MAIN_TOUCH);
	touch_manager_loop();
	#endif

	#if HAS_BLE_HID
	if (keyboard_hid_transport() == KeyboardTransport::Ble) {
		ble_hid_loop();
	}
	#endif

	#if HAS_BLE_HID || HAS_USB_HID
	keyboard_hid_loop();
	#endif

	#if HAS_USB_HID
	mouse_hid_loop();
	gamepad_hid_loop();
	#endif

	// Handle web portal (DNS for captive portal)
	device_telemetry_mark_main_loop(DEVICE_RUNTIME_PHASE_MAIN_PORTAL);
	web_portal_handle();

	#if HAS_NATIVE_EXTENSIONS
	native_extension_loop();
	#endif

	#if HAS_MQTT
	device_telemetry_mark_main_loop(DEVICE_RUNTIME_PHASE_MAIN_NETWORK);
	if (!ota_activity_is_active()) {
	mqtt_manager.loop();
	mqtt_screen_loop();
	mqtt_wake_loop();
	mqtt_audio_loop();
	mqtt_camera_loop();
	mqtt_notify_loop();
#if MQTT_TRIGGERS_ENABLED
		mqtt_triggers_loop();
#endif
	}
	#endif

	// Allow sensors to flush ISR-deferred work (e.g., instant MQTT publishes).
	device_telemetry_mark_main_loop(DEVICE_RUNTIME_PHASE_MAIN_SENSORS);
	sensor_manager_loop();

	// Process hardware button debounce/hold + action dispatch (no-op when !HAS_BUTTON).
	device_telemetry_mark_main_loop(DEVICE_RUNTIME_PHASE_MAIN_BUTTONS);
	hw_buttons_loop();



	unsigned long current_ms = millis();

	// WiFi watchdog - monitor connection and reconnect if needed
	// Only run if we're not in AP mode (AP mode is the fallback, should stay active)
	wifi_manager_watchdog(&device_config, config_loaded,
			g_setup_ap_active || web_portal_is_ap_mode());

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
