#include <cassert>
#include <cstdio>

#include "pad_layout.h"

extern const lv_font_t lv_font_montserrat_12 = {};
extern const lv_font_t lv_font_montserrat_14 = {};
extern const lv_font_t lv_font_montserrat_18 = {};
extern const lv_font_t lv_font_montserrat_24 = {};
extern const lv_font_t lv_font_montserrat_32 = {};
extern const lv_font_t lv_font_montserrat_36 = {};
extern const lv_font_t lv_font_montserrat_48 = {};

static void test_layout_spacing_and_insets() {
    const uint8_t positions[] = {0, 1, 0, 1};
    const uint8_t spans[] = {1, 1, 1, 1};
    PadRect rects[4] = {};
    const PadGridLayoutSpace layout = {8, 12, 4, 16, 6, 6, 4};

    pad_compute_grid(2, 2, 100, 100, positions, positions + 2,
                     spans, spans, 4, rects, &layout);

    assert(rects[0].x == layout.pixel_shift_margin + layout.left);
    assert(rects[0].y == layout.pixel_shift_margin + layout.top);
    assert(rects[1].x == rects[0].x + rects[0].w + layout.gap_x);
    assert(rects[2].y == rects[0].y + rects[0].h + layout.gap_y);
    assert(rects[1].x + rects[1].w + layout.right <= 100 - layout.pixel_shift_margin);
    assert(rects[2].y + rects[2].h + layout.bottom <= 100 - layout.pixel_shift_margin);

    const PadGridLayoutSpace no_shift = {0, 0, 0, 0, 0, 0, 0};
    pad_compute_grid(1, 1, 100, 100, positions, positions + 2,
                     spans, spans, 1, rects, &no_shift);
    assert(rects[0].x == 0);
    assert(rects[0].y == 0);
    assert(rects[0].w == 100);
    assert(rects[0].h == 100);
}

int main() {
    test_layout_spacing_and_insets();
    std::puts("pad_layout: PASS");
    return 0;
}