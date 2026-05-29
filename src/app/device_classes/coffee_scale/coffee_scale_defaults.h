#pragma once

// Coffee Scale compile-time defaults.
//
// Owned by the coffee_scale device class. Shared code (board_config.h,
// config_manager, etc.) MUST NOT reference these macros — they are not in
// scope on non-coffee-scale builds. Board override files
// (src/boards/<name>/board_overrides.h) may pre-define any of these macros;
// the #ifndef guards below preserve those overrides.
//
// This header is gated on IS_COFFEE_SCALE so it is safe to include
// unconditionally — non-coffee-scale builds get an empty translation unit.

#include "board_config.h"   // for IS_COFFEE_SCALE

#if IS_COFFEE_SCALE

// HX711 strain-gauge amplifier (bit-banged digital protocol).
#ifndef HAS_SENSOR_HX711
#define HAS_SENSOR_HX711 false
#endif

// NAU7802 24-bit I2C load-cell ADC.
#ifndef HAS_SENSOR_NAU7802
#define HAS_SENSOR_NAU7802 false
#endif

// Derived: true when any scale-capable sensor backend is enabled. Used by
// sensor-driver gates and config-manager NVS-key compile-out logic in this
// device class.
#ifndef HAS_SCALE
#define HAS_SCALE (HAS_SENSOR_HX711 || HAS_SENSOR_NAU7802)
#endif

// HX711 data-out pin. -1 disables the driver even when HAS_SENSOR_HX711 is true.
#ifndef HX711_DOUT_PIN
#define HX711_DOUT_PIN -1
#endif

// HX711 clock pin. -1 disables the driver even when HAS_SENSOR_HX711 is true.
#ifndef HX711_SCK_PIN
#define HX711_SCK_PIN -1
#endif

#endif // IS_COFFEE_SCALE
