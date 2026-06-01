#pragma once

// Darkroom Timer compile-time defaults.
//
// Owned by the darkroom_timer device class. Shared code (board_config.h,
// config_manager, etc.) MUST NOT reference these macros — they are not in
// scope on non-darkroom-timer builds. Board override files
// (src/boards/<name>/board_overrides.h) may pre-define any of these macros;
// the #ifndef guards below preserve those overrides.
//
// This header is gated on IS_DARKROOM_TIMER so it is safe to include
// unconditionally — non-darkroom-timer builds get an empty translation unit.

#include "board_config.h"   // for IS_DARKROOM_TIMER

#if IS_DARKROOM_TIMER

// TSL2591 high-dynamic-range I2C light sensor (used for enlarger metering).
#ifndef HAS_SENSOR_TSL2591
#define HAS_SENSOR_TSL2591 false
#endif

// I2C bus index for the TSL2591. A dedicated bus (e.g. Wire1) keeps sensor
// reads off the touch controller's bus so metering never blocks touch polling.
#ifndef TSL2591_I2C_BUS
#define TSL2591_I2C_BUS 0
#endif

// TSL2591 SDA pin. -1 disables the driver even when HAS_SENSOR_TSL2591 is true.
#ifndef TSL2591_I2C_SDA
#define TSL2591_I2C_SDA -1
#endif

// TSL2591 SCL pin. -1 disables the driver even when HAS_SENSOR_TSL2591 is true.
#ifndef TSL2591_I2C_SCL
#define TSL2591_I2C_SCL -1
#endif

// TSL2591 I2C bus clock. The sensor tops out at 400 kHz (fast mode).
#ifndef TSL2591_I2C_FREQUENCY
#define TSL2591_I2C_FREQUENCY 400000
#endif

#endif // IS_DARKROOM_TIMER
