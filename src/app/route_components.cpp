// ============================================================================
// route_components.cpp — aggregation translation unit for device-class /
// feature web routes.
//
// arduino-cli only compiles .cpp files in the sketch root, so .cpp files that
// live under src/app/device_classes/**/web/ or other subdirectories must be
// #include'd here. Each included translation unit registers its routes via
// REGISTER_ROUTES() (see web_portal_routes.h); the static initializer runs at
// startup and the routes are wired by web_portal_routes_register_all() called
// from web_portal_register_routes().
//
// This file is strictly for HTTP route registration files. Other subsystem
// aggregation lives in portal_components.cpp, widgets.cpp, etc.
// ============================================================================

#include "board_config.h"

#if IS_SHUTTER_TESTER
#include "device_classes/shutter_tester/web/portal_shutter_sessions.cpp"
#include "device_classes/shutter_tester/web/portal_shutter_tests.cpp"
#endif

#if IS_COFFEE_SCALE
#include "device_classes/coffee_scale/web/web_portal_scale.cpp"
#include "device_classes/coffee_scale/web/web_portal_brews.cpp"
#include "device_classes/coffee_scale/web/web_portal_brew_templates.cpp"
#endif

#if IS_DARKROOM_TIMER
#include "device_classes/darkroom_timer/web_portal_relay.cpp"
#include "device_classes/darkroom_timer/web_portal_prints.cpp"
#endif
