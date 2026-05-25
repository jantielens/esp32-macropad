#pragma once

#include "board_config.h"

#if HAS_EPAPER

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

#endif // HAS_EPAPER
