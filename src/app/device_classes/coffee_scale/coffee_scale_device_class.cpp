// Coffee Scale device-class aggregator.
//
// arduino-cli only compiles `.cpp` files in the sketch root. All coffee-scale
// translation units live under `device_classes/coffee_scale/` and are pulled
// into the build by being `#include`d from this aggregator under the
// IS_COFFEE_SCALE gate, mirroring the shutter-tester aggregation pattern
// (see device_classes/shutter_tester_device_class.cpp).
//
// Phase 2: sensors + scale HAL only. No actions, no brew engine, no portal
// UI, no NVS config_load/save wiring (those land in Phases 3-6). Each
// included .cpp gates its own contents on HAS_SCALE / HAS_SENSOR_* so this
// aggregator does not need extra gating beyond IS_COFFEE_SCALE.

#include "board_config.h"

#if IS_COFFEE_SCALE

#include "device_class.h"
#include "log_manager.h"
#include "power_config.h"
#include "sensors/sensor_manager.h"

// Per-module translation units. Order: smoothing first (other drivers
// reference it), then sensor backends, then scale HAL (depends on backend
// headers), then scale_init (calls into future brew/binding subsystems).
#include "sensors/scale_smoothing.cpp"
#include "sensors/hx711_sensor.cpp"
#include "sensors/nau7802_sensor.cpp"
#include "scale_hal.cpp"
#include "scale_init.cpp"

#include "sensors/hx711_sensor.h"
#include "sensors/nau7802_sensor.h"
#include "scale_init.h"

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
// Fires AFTER LittleFS mount, AFTER WiFi/AP/portal init, AFTER web_portal_init,
// and BEFORE the always-on path calls sensor_manager_init() / binding inits
// (app.ino lines 407 / 423 / 464+). Phase 2 calls into the no-op skeleton
// scale_subsystem_init_storage() + scale_subsystem_init() so Phase 4 can edit
// the bodies in place without touching this aggregator.
// ---------------------------------------------------------------------------
static void on_setup_late_hook(DeviceConfig * /*config*/, PowerMode /*current_mode*/) {
    scale_subsystem_init_storage();
    scale_subsystem_init();
}

static const DeviceClass kCoffeeScaleClass = {
    /* .name              = */ "coffee_scale",
    /* .owned_mode        = */ PowerMode::AlwaysOn,
    /* .on_setup_early    = */ nullptr,
    /* .on_setup_late     = */ on_setup_late_hook,
    /* .on_loop           = */ nullptr,
    /* .run_duty_cycle    = */ nullptr,
    /* .on_wake_classify  = */ nullptr,
    /* .on_sleep_prepare  = */ nullptr,
    /* .config_defaults   = */ nullptr,
    /* .config_load       = */ nullptr,
    /* .config_save       = */ nullptr,
    /* .config_api_get    = */ nullptr,
    /* .config_api_set    = */ nullptr,
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
