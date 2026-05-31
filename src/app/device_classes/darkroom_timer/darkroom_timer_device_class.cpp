// Darkroom Timer device-class aggregator.
//
// arduino-cli only compiles `.cpp` files in the sketch root. All darkroom-timer
// translation units live under `device_classes/darkroom_timer/` and are pulled
// into the build by being `#include`d from this aggregator under the
// IS_DARKROOM_TIMER gate, mirroring the coffee-scale aggregation pattern
// (see device_classes/coffee_scale/coffee_scale_device_class.cpp).
//
// Web route modules (web_portal_relay.cpp) are aggregated separately by
// route_components.cpp, matching the coffee-scale / shutter-tester convention.

#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "device_class.h"
#include "log_manager.h"
#include "power_config.h"

// Per-module translation units. Order: low-level driver + persistence first
// (engines reference them), then the engines, then the ActionTypeDef
// registrations. Each .cpp self-gates on IS_DARKROOM_TIMER (and HAS_DISPLAY
// for the action files) internally.
#include "relay_controller.cpp"
#include "sensors/tsl2591_sensor.cpp"
#include "print_log.cpp"
#include "meter.cpp"
#include "expose_timer.cpp"
#include "test_strip.cpp"
// ActionTypeDef registrations. Each self-gates on HAS_DISPLAY &&
// IS_DARKROOM_TIMER internally.
#include "shelly_actions.cpp"
#include "expose_actions.cpp"
#include "test_strip_actions.cpp"
#include "meter_actions.cpp"
#include "print_log_actions.cpp"

#include "relay_controller.h"
#include "sensors/tsl2591_sensor.h"
#include "print_log.h"
#include "meter.h"
#include "expose_timer.h"
#include "test_strip.h"

#define TAG "DarkroomTimer"

// ---------------------------------------------------------------------------
// DeviceClass.on_setup_late hook.
//
// Fires AFTER the storage facade mount, AFTER WiFi/AP/portal init, and AFTER
// web_portal_init, so the relay task can reach the network and the relay
// config file is available on the filesystem.
// ---------------------------------------------------------------------------
static void on_setup_late_hook(DeviceConfig* /*config*/, PowerMode /*current_mode*/) {
    relay_controller_init();
    relay_load_config();
    tsl2591_init();
    print_log_init();
    meter_init();
    expose_timer_init();
    test_strip_init();
}

// Drain deferred relay actions and advance the darkroom engines on the main
// loop pump (cheap state-machine advances + deferred flash I/O).
static void on_loop_hook() {
    relay_loop();
    meter_loop();
    expose_timer_tick();
    test_strip_tick();
    print_log_loop();
}

static const DeviceClass kDarkroomTimerClass = {
    /* .name              = */ "darkroom_timer",
    /* .owned_mode        = */ PowerMode::AlwaysOn,
    /* .on_setup_early    = */ nullptr,
    /* .on_setup_late     = */ on_setup_late_hook,
    /* .on_loop           = */ on_loop_hook,
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

void darkroom_timer_device_class_register() {
    if (!device_class_register(&kDarkroomTimerClass)) {
        LOGW(TAG, "device_class_register() rejected darkroom_timer (registry full?)");
    }
}

#endif // IS_DARKROOM_TIMER
