// ============================================================================
// Coffee Scale device-class config — module-local singleton owned by the
// coffee_scale device class.
//
// These fields used to live as #if HAS_SCALE members of the shared
// DeviceConfig struct in config_manager.h. They now belong to the device
// class, with NVS load/save wired through the existing DeviceClass.config_*
// hooks (see coffee_scale_device_class.cpp).
//
// NVS keys: redesigned for 15-char limit + sensor-agnostic naming. Old keys
// were `hx711_cal` / `hx711_ofs` / `scale_sm`; the new scheme uses
// `scale_cal` / `scale_ofs` / `scale_smooth` (Phase 4 of coffee-scale port,
// per Jan's directive — clean break, no legacy compat).
// ============================================================================
#pragma once

#include "board_config.h"

#if IS_COFFEE_SCALE

#include <Preferences.h>
#include <stdint.h>

#define COFFEE_SCALE_CAL_MAX_LEN 32

struct CoffeeScaleConfig {
    // Calibration factor (HX711/NAU7802 counts per gram). Stored as string
    // for backward-compat with portal text-input handling; converted to float
    // at apply time.
    char scale_cal_factor[COFFEE_SCALE_CAL_MAX_LEN];
    // Zero offset (raw counts). Same string-vs-float story.
    char scale_offset[COFFEE_SCALE_CAL_MAX_LEN];
    // Smoothing preset: 0=Stable, 1=Balanced (default), 2=Responsive.
    uint8_t scale_smoothing;
};

// Module-local singleton. Read at sensor init; written by the load/save
// hooks below and by portal POST /api/config.
extern CoffeeScaleConfig coffee_scale_config;

// Populate config with compile-time defaults (used when no NVS data yet).
void coffee_scale_config_defaults();

// Load coffee-scale fields from the already-open NVS namespace and apply
// calibration + smoothing live. Runs the auto-tare-on-first-boot rule:
// if no calibration is present, logs a warning and requests a tare.
void coffee_scale_config_load(Preferences& prefs);

// Persist coffee-scale fields to the already-open NVS namespace.
void coffee_scale_config_save(Preferences& prefs);

#endif // IS_COFFEE_SCALE
