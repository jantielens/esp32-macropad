#ifndef BOARD_OVERRIDES_JC4880P433_NAU7802_H
#define BOARD_OVERRIDES_JC4880P433_NAU7802_H

// ============================================================================
// JC4880P433 + NAU7802 Load Cell ADC (Coffee Scale variant)
// ============================================================================
// Inherits all base board settings (display, touch, audio, etc.) from the
// generic jc4880p433 macropad board and layers on the coffee-scale product
// variant flag, NAU7802 I2C ADC pins (reused on the same physical 2-pin
// header as the HX711 variant), and the brews portal hero category.

#include "../jc4880p433/board_overrides.h"

// ============================================================================
// Product variant — selects the COFFEE_SCALE device class
// ============================================================================
#define IS_COFFEE_SCALE true

// ============================================================================
// NAU7802 I2C Load Cell ADC (Scale)
// ============================================================================
#define HAS_SENSOR_NAU7802 true
// NAU7802 communicates over I2C — reuse the same GPIOs as the HX711 header.
#define SENSOR_I2C_SDA 52   // Same physical pin as HX711_DOUT_PIN
#define SENSOR_I2C_SCL 51   // Same physical pin as HX711_SCK_PIN

// ============================================================================
// Portal Primary Category
// ============================================================================
#define PORTAL_PRIMARY_FRAGMENT "brews"
#define PORTAL_PRIMARY_CATEGORY "coffee"
#define PORTAL_PRIMARY_LABEL    "Coffee Scale"
#define PORTAL_PRIMARY_ICON     "\xe2\x98\x95"  // ☕

#endif // BOARD_OVERRIDES_JC4880P433_NAU7802_H
