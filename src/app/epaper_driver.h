#pragma once

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
void epaper_driver_display();
void epaper_driver_sleep();

// Battery voltage in millivolts (0 if not supported).
uint16_t epaper_driver_battery_mv();

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
