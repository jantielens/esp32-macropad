// ============================================================================
// Shutter Tester device-class config — module-local singleton owned by the
// shutter_tester device class.
//
// These fields used to live as #if IS_SHUTTER_TESTER members of the shared
// DeviceConfig struct in config_manager.h. They now belong to the device
// class, with NVS load/save wired through the existing DeviceClass.config_*
// hooks (see shutter_tester_device_class.cpp).
//
// Wire-format invariants: NVS keys (`sh_preset`, `sh_off_x`, `sh_off_y`) and
// field semantics are UNCHANGED across this refactor.
// ============================================================================
#pragma once

#include "board_config.h"

#if IS_SHUTTER_TESTER

#include <Preferences.h>

#define SHUTTER_CONFIG_PRESET_ID_MAX_LEN 24

struct ShutterTesterConfig {
    // Active capture preset (resolved by shutter_capture_init()).
    char preset_id[SHUTTER_CONFIG_PRESET_ID_MAX_LEN]; // e.g. "direct_3_line"
    // Sensor layout: distance from centre sensor (S2) to outer sensor (S1 or
    // S3), in mm. Used for capping gradient computation
    // (diagonal = 2 * sqrt(x² + y²)).
    float sensor_offset_x_mm; // default SHUTTER_DEFAULT_OFFSET_X_MM
    float sensor_offset_y_mm; // default SHUTTER_DEFAULT_OFFSET_Y_MM
};

// Module-local singleton. Read by shutter capture/measure setup; written by
// the load/save hooks below.
extern ShutterTesterConfig shutter_config;

// Populate `shutter_config` with compile-time defaults (used when no NVS
// config exists yet).
void shutter_config_defaults();

// Load shutter-tester fields from the already-open NVS namespace.
void shutter_config_load(Preferences& prefs);

// Persist shutter-tester fields to the already-open NVS namespace.
void shutter_config_save(Preferences& prefs);

#endif // IS_SHUTTER_TESTER
