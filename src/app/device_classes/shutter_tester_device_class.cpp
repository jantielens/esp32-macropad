// Shutter Tester device-class aggregator.
//
// arduino-cli only compiles `.cpp` files in the sketch root. All shutter-tester
// translation units live under `device_classes/shutter_tester/` and are pulled
// into the build by `#include`-ing them here under the `IS_SHUTTER_TESTER` gate,
// mirroring the e-paper aggregation pattern (see `epaper_device_class.cpp`).
//
// Ripping out the shutter tester device class requires deleting this file,
// the `device_classes/shutter_tester/` folder, and the small number of
// aggregation hooks elsewhere (see PRD §Aggregation exceptions).

#include "board_config.h"

#if IS_SHUTTER_TESTER

#include <ArduinoJson.h>

#include "config_manager.h"
#include "device_class.h"
#include "log_manager.h"
#include "power_config.h"
#include "shutter_tester/shutter_binding.h"
#include "shutter_tester/shutter_capture.h"
#include "shutter_tester/shutter_config.h"
#include "shutter_tester/shutter_measure.h"
#include "shutter_tester/shutter_session.h"
#include "shutter_tester/shutter_session_actions.h"

#if HAS_DISPLAY
#include "display_manager.h"
#endif

// Forward declarations for list providers registered late in setup. Defined
// in their respective .cpp files (aggregated below).
void list_provider_shutter_tests_init();

// ---------------------------------------------------------------------------
// Setup hook: bring up shutter sensors, measurement, and session state once
// the core has finished its display / pad / WiFi init. Mirrors the work that
// used to live as five separate #if IS_SHUTTER_TESTER blocks in app.ino.
// ---------------------------------------------------------------------------
static void on_setup_late_hook(DeviceConfig *config, PowerMode /*current_mode*/) {
#if HAS_DISPLAY
		display_manager_set_splash_status("Init sensors...");
#endif

		// List provider registration is order-independent as long as it lands
		// before any [list:shutter_tests.selected] binding resolves, which only
		// happens during pad rendering after this hook returns.
		list_provider_shutter_tests_init();

		shutter_capture_init(shutter_config.preset_id);
		shutter_measure_init();

		// Set sensor geometry from the resolved preset. For Direct3Line, the
		// shutter_config offsets override the preset defaults (user-configurable
		// mount dimensions); for other presets, use the preset-defined positions
		// directly.
		ShutterCaptureCaps caps = {};
		shutter_capture_get_caps(&caps);
		if (caps.preset_id == ShutterPresetId::Direct3Line) {
				ShutterSensorPosition pos3[3] = {
						{ -shutter_config.sensor_offset_x_mm, -shutter_config.sensor_offset_y_mm },
						{  0.0f,                              0.0f                              },
						{  shutter_config.sensor_offset_x_mm,  shutter_config.sensor_offset_y_mm },
				};
				shutter_measure_set_geometry(pos3, 3);
		} else {
				ShutterSensorPosition pos_buf[SHUTTER_SENSOR_MAX];
				uint8_t count = shutter_capture_get_positions(pos_buf, SHUTTER_SENSOR_MAX);
				shutter_measure_set_geometry(pos_buf, count);
		}

		shutter_session_init();
		shutter_session_actions_init();
		shutter_binding_init();
}

// ---------------------------------------------------------------------------
// Loop hook: drain action queue and process newly-armed captures every tick.
// Both are cheap polls; neither blocks.
// ---------------------------------------------------------------------------
static void on_loop_hook() {
		shutter_session_actions_loop();
		shutter_measure_process();
}

// ---------------------------------------------------------------------------
// Config hooks: bridge the shared DeviceClass.config_* dispatchers to the
// module-local shutter_config singleton. The DeviceConfig* argument is
// intentionally ignored; shutter-tester fields no longer live there.
// ---------------------------------------------------------------------------
static void on_config_defaults_hook(DeviceConfig * /*config*/) {
		shutter_config_defaults();
}

static void on_config_load_hook(DeviceConfig * /*config*/, Preferences &preferences) {
		shutter_config_load(preferences);
}

static void on_config_save_hook(const DeviceConfig * /*config*/, Preferences &preferences) {
		shutter_config_save(preferences);
}

// Expose shutter_config fields on GET /api/config so the portal Sensor
// Configuration fragment can load them. Matches the field names registered
// via window.registerConfigFields() in portal_shutter_config.js.
static void config_api_get_hook(const DeviceConfig * /*config*/, JsonObject &root) {
		root["shutter_preset_id"] = shutter_config.preset_id;
		root["sensor_offset_x_mm"] = shutter_config.sensor_offset_x_mm;
		root["sensor_offset_y_mm"] = shutter_config.sensor_offset_y_mm;
}

// Apply shutter_config fields from POST /api/config. The post handler runs
// config_save() after dispatch, which persists shutter_config via the
// on_config_save_hook above.
static void config_api_set_hook(DeviceConfig * /*config*/, JsonObject &body) {
		if (body.containsKey("shutter_preset_id")) {
				const char *v = body["shutter_preset_id"] | "";
				strncpy(shutter_config.preset_id, v, SHUTTER_CONFIG_PRESET_ID_MAX_LEN - 1);
				shutter_config.preset_id[SHUTTER_CONFIG_PRESET_ID_MAX_LEN - 1] = '\0';
		}
		if (body.containsKey("sensor_offset_x_mm")) {
				shutter_config.sensor_offset_x_mm = body["sensor_offset_x_mm"].as<float>();
		}
		if (body.containsKey("sensor_offset_y_mm")) {
				shutter_config.sensor_offset_y_mm = body["sensor_offset_y_mm"].as<float>();
		}
}

// ---------------------------------------------------------------------------
// Class instance + registration. No owned power mode (shutter tester runs
// in the always-on display path), no MQTT hooks (the shutter modules publish
// their own state via the binding engine).
// ---------------------------------------------------------------------------
static const DeviceClass kShutterTesterClass = {
		/* name */              "shutter_tester",
		/* owned_mode */        PowerMode::AlwaysOn,
		/* on_setup_early */    nullptr,
		/* on_setup_late */     on_setup_late_hook,
		/* on_loop */           on_loop_hook,
		/* run_duty_cycle */    nullptr,
		/* on_wake_classify */  nullptr,
		/* on_sleep_prepare */  nullptr,
		/* config_defaults */   on_config_defaults_hook,
		/* config_load */       on_config_load_hook,
		/* config_save */       on_config_save_hook,
		/* config_api_get */    config_api_get_hook,
		/* config_api_set */    config_api_set_hook,
		/* mqtt_on_discovery */ nullptr,
		/* mqtt_publish_state */ nullptr,
		/* pad_hold_scheme */   "[shutter:",
		/* pad_hold_acquire */  shutter_capture_acquire,
		/* pad_hold_release */  shutter_capture_release,
};

void shutter_tester_device_class_register() {
		device_class_register(&kShutterTesterClass);
}

// ---------------------------------------------------------------------------
// Aggregate the rest of the shutter-tester translation units into this build.
// arduino-cli only compiles `.cpp` files in the sketch root; everything under
// device_classes/shutter_tester/ is brought in via these #includes so the
// whole device class lives in one folder.
// ---------------------------------------------------------------------------

// Core modules
#include "shutter_tester/shutter_adc.cpp"
#include "shutter_tester/shutter_capture.cpp"
#include "shutter_tester/shutter_measure.cpp"
#include "shutter_tester/shutter_curtain_stats.cpp"
#include "shutter_tester/shutter_session.cpp"
#include "shutter_tester/shutter_session_actions.cpp"
#include "shutter_tester/shutter_test_scripts.cpp"
#include "shutter_tester/shutter_binding.cpp"
#include "shutter_tester/shutter_align_binding.cpp"
#include "shutter_tester/shutter_actions.cpp"

// Web portal endpoints and list providers
#include "shutter_tester/web/list_provider_shutter_tests.cpp"
// NOTE: shutter_config.cpp is aggregated from portal_components.cpp per
// project convention (see portal_components.cpp IS_SHUTTER_TESTER block).

#endif // IS_SHUTTER_TESTER
