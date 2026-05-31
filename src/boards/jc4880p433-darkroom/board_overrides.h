#ifndef BOARD_OVERRIDES_JC4880P433_DARKROOM_H
#define BOARD_OVERRIDES_JC4880P433_DARKROOM_H

// ============================================================================
// JC4880P433 + TSL2591 Light Sensor + Shelly Relays (Darkroom Timer variant)
// ============================================================================
// Inherits all base board settings (display, touch, audio, etc.) from the
// generic jc4880p433 macropad board and layers on the darkroom-timer product
// variant flag and the TSL2591 ambient-light metering sensor wired to the
// secondary I2C bus. Enlarger and safelight switching is handled over Wi-Fi
// through Shelly relays, so no relay GPIOs are needed on the board itself.

#include "../jc4880p433/board_overrides.h"

// ============================================================================
// Product variant — selects the DARKROOM_TIMER device class
// ============================================================================
#define IS_DARKROOM_TIMER true

// ============================================================================
// TSL2591 Light Sensor (I2C metering)
// ============================================================================
// Dedicated secondary I2C bus so the metering sensor never contends with the
// GT911 touch controller / ES8311 codec on bus 0.
#define HAS_SENSOR_TSL2591 true
#define TSL2591_I2C_BUS       1
#define TSL2591_I2C_SDA       52
#define TSL2591_I2C_SCL       51
#define TSL2591_I2C_FREQUENCY 400000

// ============================================================================
// Web portal — promote the Darkroom category to the primary nav position
// ============================================================================
// Lands the portal on the darkroom relay/config page and surfaces a welcome
// hero card, matching the shutter-tester / coffee-scale primary-category
// pattern. The fragment id must match a REGISTER_NAV_COMPONENT entry in the
// "darkroom" category (see device_classes/darkroom_timer/components/).
#define PORTAL_PRIMARY_FRAGMENT "darkroom"
#define PORTAL_PRIMARY_CATEGORY "darkroom"
#define PORTAL_PRIMARY_LABEL    "Darkroom Timer"
#define PORTAL_PRIMARY_ICON     "\xf0\x9f\x94\xb4"  // 🔴

#endif // BOARD_OVERRIDES_JC4880P433_DARKROOM_H
