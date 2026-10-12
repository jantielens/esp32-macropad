// ============================================================================
// Test stub: board_config.h — shadows the real ESP32 board_config.h
// ============================================================================
// Provides minimal defines so binding code compiles on the host.

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define HAS_DISPLAY true
#define HAS_MQTT    true
#define HAS_BLE_HID true
#define HAS_USB_HID false
#define IS_SHUTTER_TESTER true

// Host tests exercise the PSRAM-gated feature set (mirrors a PSRAM board).
#ifndef HAS_PSRAM
#define HAS_PSRAM 1
#endif
#ifndef HAS_HA_HISTORY
#define HAS_HA_HISTORY (HAS_DISPLAY && HAS_MQTT && HAS_PSRAM)
#endif
#ifndef HA_HISTORY_MIN_SLOT_SECS
#define HA_HISTORY_MIN_SLOT_SECS 300
#endif

#ifndef MAX_PADS
#define MAX_PADS 16
#endif
#ifndef ACTION_CONTINUATION_SLOTS
#define ACTION_CONTINUATION_SLOTS 3
#endif
#ifndef UI_SCALE_TIER
#define UI_SCALE_TIER 0
#endif
#ifndef DEFAULT_PIXEL_SHIFT_DISTANCE
#define DEFAULT_PIXEL_SHIFT_DISTANCE 4
#endif
#ifndef DEFAULT_WIDGET_SERIES_COLOR
#define DEFAULT_WIDGET_SERIES_COLOR "#4CAF50"
#define DEFAULT_WIDGET_SERIES_COLOR_2 "#2196F3"
#define DEFAULT_WIDGET_SERIES_COLOR_3 "#9C27B0"
#define DEFAULT_WIDGET_SERIES_COLOR_4 "#FF9800"
#define DEFAULT_WIDGET_TRACK_COLOR "#1A1A1A"
#define DEFAULT_WIDGET_INDICATOR_COLOR "#FFFFFF"
#define DEFAULT_WIDGET_TICK_COLOR "#808080"
#define DEFAULT_WIDGET_ZONE_COLOR "#FF5722"
#define DEFAULT_WIDGET_REFERENCE_COLOR "#888888"
#define DEFAULT_WIDGET_TABLE_BG_COLOR "#12122a"
#define DEFAULT_WIDGET_TABLE_FG_COLOR "#b0b0d0"
#define DEFAULT_WIDGET_TABLE_HEADER_COLOR "#404070"
#endif

// Shutter verdict thresholds (mirrors real src/app/board_config.h defaults).
// Needed in the host stub because shutter_measure.h now lives in a
// device_classes/ subfolder, so `#include "board_config.h"` from that file
// no longer resolves to the real header via relative search.
#ifndef SHUTTER_VERDICT_DEVIATION_WARNING
#define SHUTTER_VERDICT_DEVIATION_WARNING 0.333f
#endif
#ifndef SHUTTER_VERDICT_DEVIATION_FAIL
#define SHUTTER_VERDICT_DEVIATION_FAIL 0.500f
#endif
#ifndef SHUTTER_FILM_DIAGONAL_MM
#define SHUTTER_FILM_DIAGONAL_MM 43.27f
#endif
#ifndef SHUTTER_DEFAULT_OFFSET_X_MM
#define SHUTTER_DEFAULT_OFFSET_X_MM 11.2f
#endif
#ifndef SHUTTER_DEFAULT_OFFSET_Y_MM
#define SHUTTER_DEFAULT_OFFSET_Y_MM 7.4f
#endif
#ifndef SHUTTER_DEFAULT_PRESET_ID
#define SHUTTER_DEFAULT_PRESET_ID "direct_3_line"
#endif
#ifndef SHUTTER_SENSOR_MAX
#define SHUTTER_SENSOR_MAX 9
#endif
#ifndef SHUTTER_PRE_TRIGGER_SAMPLES
#define SHUTTER_PRE_TRIGGER_SAMPLES 4096
#endif
#ifndef SHUTTER_POST_CAPTURE_SAMPLES
#define SHUTTER_POST_CAPTURE_SAMPLES 4096
#endif
#ifndef SHUTTER_ADC_PIN_S1
#define SHUTTER_ADC_PIN_S1 -1
#endif
#ifndef SHUTTER_ADC_PIN_S2
#define SHUTTER_ADC_PIN_S2 -1
#endif
#ifndef SHUTTER_ADC_PIN_S3
#define SHUTTER_ADC_PIN_S3 -1
#endif
#ifndef SHUTTER_ADC_PIN_S4
#define SHUTTER_ADC_PIN_S4 -1
#endif
#ifndef SHUTTER_ADC_PIN_S5
#define SHUTTER_ADC_PIN_S5 -1
#endif
#ifndef SHUTTER_ADC_PIN_S6
#define SHUTTER_ADC_PIN_S6 -1
#endif
#ifndef SHUTTER_ADC_PIN_S7
#define SHUTTER_ADC_PIN_S7 -1
#endif
#ifndef SHUTTER_ADC_PIN_S8
#define SHUTTER_ADC_PIN_S8 -1
#endif
#ifndef SHUTTER_ADC_PIN_S9
#define SHUTTER_ADC_PIN_S9 -1
#endif

// strlcpy is available on ESP32 (newlib) but not older glibc — declare for host
// tests. glibc 2.38+ (Ubuntu 24.04) ships strlcpy natively, so skip when present.
#include <stddef.h>
#include <string.h>
#if !defined(__GLIBC__) || (__GLIBC__ < 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 38)
#ifdef __cplusplus
extern "C"
#endif
size_t strlcpy(char* dst, const char* src, size_t siz);
#endif

#endif // BOARD_CONFIG_H
