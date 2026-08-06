// Coffee Scale device-class aggregator.
//
// arduino-cli only compiles `.cpp` files in the sketch root. All coffee-scale
// translation units live under `device_classes/coffee_scale/` and are pulled
// into the build by being `#include`d from this aggregator under the
// IS_COFFEE_SCALE gate, mirroring the shutter-tester aggregation pattern
// (see device_classes/shutter_tester_device_class.cpp).
//
// Each included .cpp gates its own contents on HAS_SCALE / HAS_SENSOR_* so
// this aggregator does not need extra gating beyond IS_COFFEE_SCALE.

#include "board_config.h"

#if IS_COFFEE_SCALE

#include "device_class.h"
#include "log_manager.h"
#include "power_config.h"
#include "sensors/sensor_manager.h"
#include <ArduinoJson.h>

// Per-module translation units. Order: smoothing first (other drivers
// reference it), then sensor backends, then scale HAL (depends on backend
// headers).
#include "sensors/scale_smoothing.cpp"
#include "sensors/hx711_sensor.cpp"
#include "sensors/nau7802_sensor.cpp"
#include "scale_hal.cpp"
// ActionTypeDef registrations for "scale" and "brew" action types.
// Each self-gates on HAS_DISPLAY && IS_COFFEE_SCALE internally.
#include "scale_actions.cpp"
#include "brew_actions.cpp"
// Brew engine + scale/brew binding schemes + NVS config singleton.
#include "scale_binding.cpp"
#include "coffee_scale_config.cpp"
#include "brew/brew_template_dsl.cpp"
#include "brew/brew_template_loader.cpp"
#include "brew/brew_templates.cpp"
#include "brew/brew_log.cpp"
#include "brew/brew_manager.cpp"
#include "brew/brew_binding.cpp"

#include "sensors/hx711_sensor.h"
#include "sensors/nau7802_sensor.h"
#include "scale_binding.h"
#include "brew/brew_binding.h"
#include "brew/brew_manager.h"
#include "brew/brew_templates.h"
#include "coffee_scale_config.h"

#define TAG "CoffeeScale"

// ---------------------------------------------------------------------------
// Sensor registration helper.
//
// Mirrors the shape of sensors.cpp::sensor_manager_register_all(): builds a
// SensorRegistry instance and calls the per-backend register_*_sensor()
// functions under their HAS_SENSOR_* gates. Called from
// coffee_scale_device_class_register() during device_classes_register_all()
// at the very top of setup(), which runs long before sensor_manager_init()
// consumes the registry. Keeping the registration here means sensors.cpp
// stays byte-identical to release/1.19.1 (PRD criterion 18).
// ---------------------------------------------------------------------------
static void coffee_scale_register_sensors() {
    SensorRegistry registry;
#if HAS_SENSOR_HX711
    register_hx711_sensor(registry);
#endif
#if HAS_SENSOR_NAU7802
    register_nau7802_sensor(registry);
#endif
}

// ---------------------------------------------------------------------------
// DeviceClass.on_setup_late hook.
//
// Fires AFTER the storage facade mount, AFTER WiFi/AP/portal init, AFTER web_portal_init,
// and BEFORE the always-on path calls sensor_manager_init() / binding inits
// (app.ino lines 407 / 423 / 464+).
// ---------------------------------------------------------------------------
static void on_setup_late_hook(DeviceConfig * /*config*/, PowerMode /*current_mode*/) {
    brew_templates_init();
    brew_manager_init();
    scale_binding_init();
    brew_binding_init();
}

// Brew tick runs on the main loop pump (cheap state-machine advance).
static void on_loop_hook() {
    brew_tick();
}

// ---------------------------------------------------------------------------
// Config hooks: bridge the shared DeviceClass.config_* dispatchers to the
// module-local CoffeeScaleConfig singleton. The DeviceConfig* argument is
// intentionally ignored; coffee-scale fields no longer live there.
// ---------------------------------------------------------------------------
static void on_config_defaults_hook(DeviceConfig * /*config*/) {
    coffee_scale_config_defaults();
}

static void on_config_load_hook(DeviceConfig * /*config*/, Preferences& prefs) {
    coffee_scale_config_load(prefs);
}

static void on_config_save_hook(const DeviceConfig * /*config*/, Preferences& prefs) {
    coffee_scale_config_save(prefs);
}

// Expose coffee_scale_config fields on GET /api/config so the portal
// Sensor Configuration fragment can load them. Matches the field names
// registered via window.registerConfigFields() in portal_action_editor_scale.js.
static void config_api_get_hook(const DeviceConfig * /*config*/, JsonObject& root) {
    root["scale_cal_factor"] = coffee_scale_config.scale_cal_factor;
    root["scale_offset"]     = coffee_scale_config.scale_offset;
    root["scale_smoothing"]  = coffee_scale_config.scale_smoothing;
}

// Apply coffee_scale_config fields from POST /api/config. The post handler
// runs config_save() after dispatch, which persists via on_config_save_hook.
// Also applies live changes (calibration + smoothing preset) so the new
// values take effect immediately without a reboot.
static void config_api_set_hook(DeviceConfig * /*config*/, JsonObject& body) {
    if (body.containsKey("scale_cal_factor")) {
        const char* v = body["scale_cal_factor"] | "";
        strlcpy(coffee_scale_config.scale_cal_factor, v, COFFEE_SCALE_CAL_MAX_LEN);
        float f = strtof(v, nullptr);
        if (f == 0.0f) f = 1.0f;
        scale_set_calibration(f);
    }
    if (body.containsKey("scale_offset")) {
        const char* v = body["scale_offset"] | "";
        strlcpy(coffee_scale_config.scale_offset, v, COFFEE_SCALE_CAL_MAX_LEN);
    }
    if (body.containsKey("scale_smoothing")) {
        uint8_t s = body["scale_smoothing"].as<uint8_t>();
        if (s >= SCALE_PRESET_COUNT) s = 1;
        coffee_scale_config.scale_smoothing = s;
        scale_apply_preset(s);
    }
}

static const DeviceClass kCoffeeScaleClass = {
    /* .name              = */ "coffee_scale",
    /* .owned_mode        = */ PowerMode::AlwaysOn,
    /* .on_setup_early    = */ nullptr,
    /* .on_setup_late     = */ on_setup_late_hook,
    /* .on_loop           = */ on_loop_hook,
    /* .run_duty_cycle    = */ nullptr,
    /* .on_wake_classify  = */ nullptr,
    /* .on_sleep_prepare  = */ nullptr,
    /* .config_defaults   = */ on_config_defaults_hook,
    /* .config_load       = */ on_config_load_hook,
    /* .config_save       = */ on_config_save_hook,
    /* .config_api_get    = */ config_api_get_hook,
    /* .config_api_set    = */ config_api_set_hook,
    /* .mqtt_on_discovery = */ nullptr,
    /* .mqtt_publish_state = */ nullptr,
};

void coffee_scale_device_class_register() {
    coffee_scale_register_sensors();
    if (!device_class_register(&kCoffeeScaleClass)) {
        LOGW(TAG, "device_class_register() rejected coffee_scale (registry full?)");
    }
}

#endif // IS_COFFEE_SCALE
