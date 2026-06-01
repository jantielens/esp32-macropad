#pragma once

// Shutter Tester compile-time defaults.
//
// Owned by the shutter_tester device class. Shared code (board_config.h,
// config_manager, etc.) MUST NOT reference these macros — they are not in
// scope on non-shutter builds. Board override files
// (src/boards/<name>/board_overrides.h) may pre-define any SHUTTER_* macro;
// the #ifndef guards below preserve those overrides.
//
// This header is gated on IS_SHUTTER_TESTER so it is safe to include
// unconditionally — non-shutter builds get an empty translation unit.

#include "board_config.h"   // for IS_SHUTTER_TESTER

#if IS_SHUTTER_TESTER

// ADC input pin for shutter sensor 1.
#ifndef SHUTTER_ADC_PIN_S1
#define SHUTTER_ADC_PIN_S1 -1
#endif
// ADC input pin for shutter sensor 2.
#ifndef SHUTTER_ADC_PIN_S2
#define SHUTTER_ADC_PIN_S2 -1
#endif
// ADC input pin for shutter sensor 3.
#ifndef SHUTTER_ADC_PIN_S3
#define SHUTTER_ADC_PIN_S3 -1
#endif
// ADC input pin for shutter sensor 4 (used by 4-corner presets).
#ifndef SHUTTER_ADC_PIN_S4
#define SHUTTER_ADC_PIN_S4 -1
#endif
// ADC input pin for shutter sensor 5 — reserved for future presets.
#ifndef SHUTTER_ADC_PIN_S5
#define SHUTTER_ADC_PIN_S5 -1
#endif
// ADC input pin for shutter sensor 6 — reserved for future presets.
#ifndef SHUTTER_ADC_PIN_S6
#define SHUTTER_ADC_PIN_S6 -1
#endif
// ADC input pin for shutter sensor 7 — reserved for future presets.
#ifndef SHUTTER_ADC_PIN_S7
#define SHUTTER_ADC_PIN_S7 -1
#endif
// ADC input pin for shutter sensor 8 — reserved for future presets.
#ifndef SHUTTER_ADC_PIN_S8
#define SHUTTER_ADC_PIN_S8 -1
#endif
// ADC input pin for shutter sensor 9 — reserved for future presets.
#ifndef SHUTTER_ADC_PIN_S9
#define SHUTTER_ADC_PIN_S9 -1
#endif

// Maximum number of sensors across all capture presets (sized for the static
// position buffer). Runtime behavior is driven by the active preset.
#ifndef SHUTTER_SENSOR_MAX
#define SHUTTER_SENSOR_MAX 9
#endif

// Default preset id used when no stored config exists or the stored value is
// empty. Valid built-in values: "direct_single", "direct_3_line",
// "direct_4_corner". See shutter_capture.h for the preset registry.
#ifndef SHUTTER_DEFAULT_PRESET_ID
#define SHUTTER_DEFAULT_PRESET_ID "direct_3_line"
#endif

// Default horizontal sensor offset from centre sensor (S2) to outer sensor
// (S1/S3), in mm.
#ifndef SHUTTER_DEFAULT_OFFSET_X_MM
#define SHUTTER_DEFAULT_OFFSET_X_MM 11.2f
#endif
// Default vertical sensor offset from centre sensor (S2) to outer sensor
// (S1/S3), in mm.
#ifndef SHUTTER_DEFAULT_OFFSET_Y_MM
#define SHUTTER_DEFAULT_OFFSET_Y_MM 7.4f
#endif

// 35mm film diagonal for full-frame capping projection (sqrt(36² + 24²)).
#ifndef SHUTTER_FILM_DIAGONAL_MM
#define SHUTTER_FILM_DIAGONAL_MM 43.27f
#endif

// Pre-trigger sample count (kept here as a fallback; boards typically override).
#ifndef SHUTTER_PRE_TRIGGER_SAMPLES
#define SHUTTER_PRE_TRIGGER_SAMPLES 4096
#endif
// Post-pulse sample count (kept here as a fallback; boards typically override).
#ifndef SHUTTER_POST_CAPTURE_SAMPLES
#define SHUTTER_POST_CAPTURE_SAMPLES 4096
#endif

// Deviation verdict threshold (stops): values at or below this are PASS.
#ifndef SHUTTER_VERDICT_DEVIATION_WARNING
#define SHUTTER_VERDICT_DEVIATION_WARNING 0.333f
#endif
// Deviation verdict threshold (stops): values above this are a definite FAIL.
#ifndef SHUTTER_VERDICT_DEVIATION_FAIL
#define SHUTTER_VERDICT_DEVIATION_FAIL 0.500f
#endif

#endif // IS_SHUTTER_TESTER
