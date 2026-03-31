#ifndef BOARD_OVERRIDES_JC4880P433_HX711_H
#define BOARD_OVERRIDES_JC4880P433_HX711_H

// ============================================================================
// JC4880P433 + HX711 Load Cell (Coffee Scale variant)
// ============================================================================
// Inherits all base board settings (display, touch, audio, etc.)
// and adds HX711 scale sensor on the two-pin header near power.

#include "../jc4880p433/board_overrides.h"

// ============================================================================
// HX711 Load Cell Sensor (Scale)
// ============================================================================
#define HAS_SENSOR_HX711 true
// HX711 data out pin (GPIO 52 — no Ethernet on this board, pin near power header).
#define HX711_DOUT_PIN 52
// HX711 clock pin (GPIO 51 — adjacent to DOUT, near power header).
#define HX711_SCK_PIN 51

#endif // BOARD_OVERRIDES_JC4880P433_HX711_H
