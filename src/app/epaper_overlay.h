#pragma once

#include "board_config.h"

#if HAS_EPAPER

#include <stdint.h>

struct DeviceConfig;

// Render the configured status overlay onto the panel framebuffer.
// Should be called between epaper_driver_draw_url() and epaper_driver_display()
// so the overlay composites on top of the dashboard image.
//
// `battery_mv` and `cycle_time_ms` are passed in so this module does not
// depend on epaper_refresh internals — the caller already has these values.
// Pass 0 if a value is not available.
void epaper_overlay_render(const DeviceConfig* config, uint16_t battery_mv, uint32_t cycle_time_ms);

#endif // HAS_EPAPER
