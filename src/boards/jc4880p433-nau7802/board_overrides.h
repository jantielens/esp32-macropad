#ifndef BOARD_OVERRIDES_JC4880P433_NAU7802_H
#define BOARD_OVERRIDES_JC4880P433_NAU7802_H

// ============================================================================
// JC4880P433 + NAU7802 Load Cell ADC (Coffee Scale variant)
// ============================================================================
// Inherits all base board settings (display, touch, audio, etc.)
// and adds NAU7802 I2C scale sensor on the same two-pin header.

#include "../jc4880p433/board_overrides.h"

// ============================================================================
// NAU7802 I2C Load Cell ADC (Scale)
// ============================================================================
#define HAS_SENSOR_NAU7802 true
// NAU7802 communicates over I2C — reuse the same GPIOs as the HX711 header.
#define SENSOR_I2C_SDA 52   // Same physical pin as HX711_DOUT_PIN
#define SENSOR_I2C_SCL 51   // Same physical pin as HX711_SCK_PIN

#endif // BOARD_OVERRIDES_JC4880P433_NAU7802_H
