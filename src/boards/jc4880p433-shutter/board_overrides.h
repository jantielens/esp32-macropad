#ifndef BOARD_OVERRIDES_JC4880P433_SHUTTER_H
#define BOARD_OVERRIDES_JC4880P433_SHUTTER_H

// ============================================================================
// JC4880P433 + BPW34 Photodiode Array (Shutter Tester variant)
// ============================================================================
// Inherits all base board settings (display, touch, audio, etc.) from the
// generic jc4880p433 macropad board and layers on the shutter-tester
// product variant flag, photodiode ADC pins, capture buffer tuning, and
// portal hero category.

#include "../jc4880p433/board_overrides.h"

// ============================================================================
// Product variant — selects the SHUTTER_TESTER device class
// ============================================================================
#define IS_SHUTTER_TESTER true

// ============================================================================
// SD Card Storage
// ============================================================================
// Route all persistent file I/O to MicroSD instead of internal LittleFS.
// Eliminates MIPI-DSI framebuffer flicker caused by flash cache disables
// during writes. Boot halts with "SD CARD MISSING" splash if no card.
#define HAS_SD_CARD     true
#define USE_SD_STORAGE  true

// ============================================================================
// Disable Network Image Fetch
// ============================================================================
// Shutter-tester pads use LittleFS/SD-backed PNG icons on widgets but never
// the network image fetcher (HTTP(S) JPEG/PNG download + decode + scale).
// The fetcher's HTTP client and decode buffers are heavy
// MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL consumers that contribute to
// fragmentation of the ~50 KB DMA-internal pool shared with AsyncTCP / LWIP
// TX pbufs on ESP-Hosted SDIO. Disabling it removes one major churn source.
#define HAS_IMAGE_FETCH false

// ============================================================================
// Shutter Tester — up to 4× BPW34 photodiodes on ADC2
// ============================================================================
// ADC input pins (ADC2_CH0–CH3 on ESP32-P4). The runtime configuration
// (ShutterTesterConfig.preset_id) selects how many sensors are active for
// a given test — 3-sensor presets ignore S4.
#define SHUTTER_ADC_PIN_S1 49   // Sensor 1 — GPIO 49
#define SHUTTER_ADC_PIN_S2 50   // Sensor 2 — GPIO 50
#define SHUTTER_ADC_PIN_S3 51   // Sensor 3 — GPIO 51
#define SHUTTER_ADC_PIN_S4 52   // Sensor 4 — GPIO 52 (optional)

// Capture buffer tuning — more baseline context for slow shutter speeds.
// 4096 samples at 27.7 kHz ≈ 148 ms of visible baseline on each side.
#define SHUTTER_PRE_TRIGGER_SAMPLES   4096
// Extra samples recorded after pulse ends, providing post-pulse baseline.
#define SHUTTER_POST_CAPTURE_SAMPLES  4096

// ============================================================================
// Portal Primary Category — Shutter Tester is the hero surface
// ============================================================================
#define PORTAL_PRIMARY_FRAGMENT "shutter-sessions"
#define PORTAL_PRIMARY_CATEGORY "camera"
#define PORTAL_PRIMARY_LABEL    "Shutter Tester"
#define PORTAL_PRIMARY_ICON     "\xf0\x9f\x93\xb7"  // 📷

#endif // BOARD_OVERRIDES_JC4880P433_SHUTTER_H
