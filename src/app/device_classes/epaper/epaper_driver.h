#pragma once

#include <stddef.h>
#ifndef EPAPER_DRIVER_H
#define EPAPER_DRIVER_H

#include "board_config.h"

#if HAS_EPAPER

#include <math.h>
#include <stdint.h>

// ----------------------------------------------------------------------------
// E-Paper Driver HAL
// ----------------------------------------------------------------------------
// Minimal interface used by duty_cycle_run() and the portal "manual refresh"
// action. Implementations live in src/app/drivers/<board>_driver.cpp and are
// aggregated by epaper_drivers.cpp.
//
// Lifecycle on each wake (duty_cycle_epaper):
//   1. epaper_driver_begin()           — power up the panel
//   2. epaper_driver_set_rotation(r)
//   3. epaper_driver_draw_url(url)     — fetch over HTTP(S) + decode + draw
//   4. epaper_driver_display()         — push framebuffer to the panel
//   5. epaper_driver_sleep()           — power down the panel before deep sleep
//
// Returns true on success. Implementations log details with LOGI/LOGW.
// ----------------------------------------------------------------------------

bool epaper_driver_begin();
void epaper_driver_set_rotation(uint8_t rotation);
bool epaper_driver_draw_url(const char* url);
#if defined(BOARD_RETERMINAL_E1003)
bool epaper_driver_prepare_service_blob(const uint8_t* data, size_t len, const char* media_type,
        uint8_t** prepared_data, size_t* prepared_len);
bool epaper_driver_draw_service_blob(const uint8_t* data, size_t len, const char* media_type,
        const uint8_t* prepared_data, size_t prepared_len);
#endif
void epaper_driver_display();
void epaper_driver_sleep();

// Battery voltage in millivolts (0 if not supported).
uint16_t epaper_driver_battery_mv();

// ----------------------------------------------------------------------------
// Optional asynchronous panel init (wake-time overlap).
// ----------------------------------------------------------------------------
// epaper_driver_begin_async() kicks off epaper_driver_begin() so the caller can
// run independent work (e.g. the WiFi association) concurrently with the slow
// panel power-up. epaper_driver_begin_join() blocks until init has finished and
// returns the begin() result. The pair is always safe to call: on boards
// without a background-init path, begin_async() runs begin() synchronously and
// join() simply returns the cached result.
//
// epaper_driver_battery_ready_before_begin() reports whether the board can read
// the battery voltage before the panel is begun. When true the duty-cycle hook
// reads the cell and runs its low-battery gate up front, then overlaps panel
// init with the WiFi connect. When false (e.g. panels whose battery sense is
// gated behind begin()), the caller must begin() the panel first.
void epaper_driver_begin_async();
bool epaper_driver_begin_join();
bool epaper_driver_battery_ready_before_begin();

// ----------------------------------------------------------------------------
// Optional SD image cache (boards with a shared-bus microSD slot only).
// When enabled, epaper_driver_draw_url() serves the image straight from SD on
// a cache hit (skipping the multi-second HTTP body download) and stages a
// freshly downloaded image for write-back. All three are no-ops on boards
// without an SD cache.
//
// On boards that define EPAPER_SD_CS_PIN the driver implements these as thin
// pass-throughs to the shared epaper_sd_cache module. On boards without it,
// they resolve to inline no-ops provided by epaper_sd_cache.h (included below)
// — the single source of truth, so individual drivers do not define stubs.
// ----------------------------------------------------------------------------
// Enable/disable SD caching for subsequent draw_url() calls (call before draw).
void epaper_driver_set_sd_cache_enabled(bool enabled);
// Write the image staged by the last successful download to SD. Call after
// epaper_driver_display() so the ~1-2 s write lands in the awake tail rather
// than the wake-to-visible path. No-op when nothing is staged.
void epaper_driver_cache_flush();
// Wipe the on-SD image cache (portal "Clear SD cache" action). Returns true
// when the cache was cleared (or was already empty).
bool epaper_driver_sd_cache_clear();

// Provides the module API on SD boards, and the inline no-op fallbacks for the
// three vtable functions above on boards without EPAPER_SD_CS_PIN.
#include "epaper_sd_cache.h"

// ----------------------------------------------------------------------------
// GFX primitives — thin pass-through to the underlying Adafruit_GFX panel.
// Used by status screens (boot splash, error, low battery, config) and the
// on-image overlay. All are no-ops when the panel has not been begun.
// ----------------------------------------------------------------------------

// Grayscale color constants (3-bit, 0..7) for the Inkplate 3-bit mode and
// other compatible panels. Higher values = lighter.
#define EPAPER_BLACK      0
#define EPAPER_DARK_GRAY  2
#define EPAPER_LIGHT_GRAY 5
#define EPAPER_WHITE      7

// Font size IDs. Mapped to bundled Inter fonts in the driver implementation:
//   SMALL  -> Inter Regular  8pt   (reserved for high-DPI / large e-paper
//                                   boards; too small to read on Inkplate 5V2)
//   MEDIUM -> Inter Regular 12pt   (overlay, body, status detail)
//   LARGE  -> Inter Bold    20pt   (headings)
#define EPAPER_FONT_SMALL  0
#define EPAPER_FONT_MEDIUM 1
#define EPAPER_FONT_LARGE  2

void epaper_driver_clear();
void epaper_driver_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void epaper_driver_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
void epaper_driver_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color);
void epaper_driver_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color);
void epaper_driver_set_font(uint8_t font_id);         // EPAPER_FONT_SMALL/MEDIUM/LARGE
void epaper_driver_set_text_color(uint8_t color);
void epaper_driver_set_cursor(int16_t x, int16_t y);
void epaper_driver_print(const char* text);
void epaper_driver_get_text_bounds(const char* text, int16_t x, int16_t y,
                                   int16_t* x1, int16_t* y1,
                                   uint16_t* w, uint16_t* h);
int16_t epaper_driver_width();
int16_t epaper_driver_height();

// ----------------------------------------------------------------------------
// VCOM management (TPS65186-backed panels). All operations power-cycle the
// PMIC internally; they are safe to call from portal handlers with the panel
// in a sleeping state. read_vcom returns NAN on I²C failure.
// write_vcom programs the TPS65186 EEPROM (~100K-cycle endurance — the
// portal MUST warn users before invoking).
// ----------------------------------------------------------------------------
float epaper_driver_read_vcom();
bool  epaper_driver_write_vcom(float vcom);
// Show the calibration test pattern. When `preview_vcom` is finite and in the
// valid range (-5.0 .. <0.0 V) the panel is driven with that VCOM via the
// TPS65186 volatile registers only — EEPROM is left untouched, so the
// preview reverts on the next power cycle. NaN (default) uses the currently
// programmed EEPROM value.
void  epaper_driver_show_vcom_test_pattern(float preview_vcom = NAN);

// ----------------------------------------------------------------------------
// Frontlight (Inkplate 6 Flick and similar). Gated by HAS_EPAPER_FRONTLIGHT
// at the board level so callers do not need their own #ifdef around the
// declarations — disabled builds get a link error if they accidentally call
// these, which is the desired loud failure mode.
// ----------------------------------------------------------------------------
#if HAS_EPAPER_FRONTLIGHT
void epaper_driver_frontlight_on(uint8_t brightness);  // 0..63; 0 is no-op
void epaper_driver_frontlight_off();
#endif

#endif // HAS_EPAPER

#endif // EPAPER_DRIVER_H
