#ifndef EPAPER_CAROUSEL_H
#define EPAPER_CAROUSEL_H

#include "board_config.h"

#if HAS_EPAPER

#include <stdint.h>

// Carousel RTC index: persists across deep sleep, resets to 0 on power loss.
extern uint8_t g_epaper_carousel_index;

// Compute the next carousel index based on the current index and stay flag.
// Pure function with no side effects.
//
// @param current   The current carousel index (0..carousel_count-1)
// @param count     The total number of carousel entries (0..5)
// @param stay      If true, return current (pause rotation). If false, advance.
//
// @return          The next index to display. If count <= 1 or stay=true,
//                  returns current. Otherwise returns (current + 1) % count.
uint8_t epaper_carousel_next_index(uint8_t current, uint8_t count, bool stay);

#endif // HAS_EPAPER

#endif // EPAPER_CAROUSEL_H
