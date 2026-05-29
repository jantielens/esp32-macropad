#ifndef BOARD_OVERRIDES_JC4880P433_HX711_H
#define BOARD_OVERRIDES_JC4880P433_HX711_H

// ============================================================================
// JC4880P433 + HX711 Load Cell (Coffee Scale variant)
// ============================================================================
// Inherits all base board settings (display, touch, audio, etc.) from the
// generic jc4880p433 macropad board and layers on the coffee-scale product
// variant flag, HX711 amplifier pins, and the brews portal hero category.

#include "../jc4880p433/board_overrides.h"

// ============================================================================
// Product variant — selects the COFFEE_SCALE device class
// ============================================================================
#define IS_COFFEE_SCALE true

// ============================================================================
// HX711 Load Cell Sensor (Scale)
// ============================================================================
#define HAS_SENSOR_HX711 true
// HX711 data out pin (GPIO 52 — no Ethernet on this board, pin near power header).
#define HX711_DOUT_PIN 52
// HX711 clock pin (GPIO 51 — adjacent to DOUT, near power header).
#define HX711_SCK_PIN 51

// ============================================================================
// Portal Primary Category
// ============================================================================
#define PORTAL_PRIMARY_FRAGMENT "brews"
#define PORTAL_PRIMARY_CATEGORY "coffee"
#define PORTAL_PRIMARY_LABEL    "Coffee Scale"
#define PORTAL_PRIMARY_ICON     "\xe2\x98\x95"  // ☕

#endif // BOARD_OVERRIDES_JC4880P433_HX711_H
