#include "board_config.h"

#if HAS_CUSTOM_FONTS

// ============================================================================
// Custom Fonts — compilation unit
// ============================================================================
// Arduino build system only compiles .cpp files in the sketch root directory.
// This translation unit includes all generated font .c files so they are
// compiled into the firmware.

#include "fonts/font_dseg7_12.c"
#include "fonts/font_dseg7_14.c"
#include "fonts/font_dseg7_18.c"
#include "fonts/font_dseg7_24.c"
#include "fonts/font_dseg7_32.c"
#include "fonts/font_dseg7_36.c"
#include "fonts/font_dseg7_48.c"

#include "fonts/font_bebas_12.c"
#include "fonts/font_bebas_14.c"
#include "fonts/font_bebas_18.c"
#include "fonts/font_bebas_24.c"
#include "fonts/font_bebas_32.c"
#include "fonts/font_bebas_36.c"
#include "fonts/font_bebas_48.c"

#include "fonts/font_doto_12.c"
#include "fonts/font_doto_14.c"
#include "fonts/font_doto_18.c"
#include "fonts/font_doto_24.c"
#include "fonts/font_doto_32.c"
#include "fonts/font_doto_36.c"
#include "fonts/font_doto_48.c"

// ============================================================================
// Font Lookup — resolve family + size to lv_font_t*
// ============================================================================
#include "fonts/custom_fonts.h"

// Helper: find nearest available size from a sorted array of {size, font*} pairs.
struct FontEntry { uint8_t size; const lv_font_t* font; };

static const lv_font_t* find_nearest(const FontEntry* entries, int count, uint8_t size) {
    // Exact match first
    for (int i = 0; i < count; i++) {
        if (entries[i].size == size) return entries[i].font;
    }
    // Find nearest by minimum distance
    int best = 0;
    int best_dist = 9999;
    for (int i = 0; i < count; i++) {
        int dist = (int)size - (int)entries[i].size;
        if (dist < 0) dist = -dist;
        if (dist < best_dist) { best_dist = dist; best = i; }
    }
    return entries[best].font;
}

const lv_font_t* pad_custom_font_lookup(uint8_t family, uint8_t size) {
    static const FontEntry dseg7_fonts[] = {
        {12, &font_dseg7_12}, {14, &font_dseg7_14}, {18, &font_dseg7_18},
        {24, &font_dseg7_24}, {32, &font_dseg7_32}, {36, &font_dseg7_36},
        {48, &font_dseg7_48}
    };
    static const FontEntry bebas_fonts[] = {
        {12, &font_bebas_12}, {14, &font_bebas_14}, {18, &font_bebas_18},
        {24, &font_bebas_24}, {32, &font_bebas_32}, {36, &font_bebas_36},
        {48, &font_bebas_48}
    };
    static const FontEntry doto_fonts[] = {
        {12, &font_doto_12}, {14, &font_doto_14}, {18, &font_doto_18},
        {24, &font_doto_24}, {32, &font_doto_32}, {36, &font_doto_36},
        {48, &font_doto_48}
    };

    switch (family) {
        case FONT_FAMILY_DSEG7:
            return find_nearest(dseg7_fonts, 7, size);
        case FONT_FAMILY_BEBAS:
            return find_nearest(bebas_fonts, 7, size);
        case FONT_FAMILY_DOTO:
            return find_nearest(doto_fonts, 7, size);
        default:
            return nullptr;
    }
}

#endif // HAS_CUSTOM_FONTS
