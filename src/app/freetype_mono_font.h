#pragma once
#include "board_config.h"

#if HAS_DISPLAY && HAS_TTF_FONTS && HAS_FREETYPE_FONTS
#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>
#if HAS_DISPLAY_DIAGNOSTICS
#include "display_perf.h"
#endif

lv_font_t* freetype_mono_font_create(const uint8_t* data, size_t length, uint16_t size);
void freetype_mono_font_destroy(lv_font_t* font);
#if HAS_DISPLAY_DIAGNOSTICS
// Read only under LVGL ownership; health polling uses the published display snapshot.
DisplayGlyphStats freetype_mono_font_get_stats();
#endif
#endif