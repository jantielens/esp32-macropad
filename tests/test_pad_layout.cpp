#include <cassert>
#include <cstdio>
#include <initializer_list>

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
    const uint8_t rows[] = {0, 0, 1, 1};
    const uint8_t spans[] = {1, 1, 1, 1};
    PadRect rects[4] = {};
    const PadGridLayoutSpace layout = {8, 12, 4, 16, 6, 6, 4};
    const DisplayContentRect content = display_safe_content_rect(100, 100);

    pad_compute_grid(2, 2, 100, 100, positions, rows,
                     spans, spans, 4, rects, &layout);

    assert(rects[0].x == content.x + layout.pixel_shift_margin + layout.left);
    assert(rects[0].y == content.y + layout.pixel_shift_margin + layout.top);
    assert(rects[1].x == rects[0].x + rects[0].w + layout.gap_x);
    assert(rects[2].y == rects[0].y + rects[0].h + layout.gap_y);
    assert(rects[1].x + rects[1].w + layout.right <= content.x + content.w - layout.pixel_shift_margin);
    assert(rects[2].y + rects[2].h + layout.bottom <= content.y + content.h - layout.pixel_shift_margin);

    const PadGridLayoutSpace no_shift = {0, 0, 0, 0, 0, 0, 0};
    pad_compute_grid(1, 1, 100, 100, positions, rows,
                     spans, spans, 1, rects, &no_shift);
    assert(rects[0].x == content.x);
    assert(rects[0].y == content.y);
    assert(rects[0].w == content.w);
    assert(rects[0].h == content.h);
}

static void test_stopwatch_466_square_round_viewport() {
    constexpr uint16_t size = 466;
    uint8_t columns[25], rows[25], spans[25];
    for (uint8_t index = 0; index < 25; ++index) {
        columns[index] = index % 5;
        rows[index] = index / 5;
        spans[index] = 1;
    }
    PadRect rects[25] = {};
    pad_compute_grid(5, 5, size, size, columns, rows, spans, spans, 25, rects);
    for (const PadRect& rect : rects) {
        assert(rect.w > 0 && rect.h > 0);
        assert(rect.x >= DEFAULT_PIXEL_SHIFT_DISTANCE);
        assert(rect.y >= DEFAULT_PIXEL_SHIFT_DISTANCE);
        assert(rect.x + rect.w <= size - DEFAULT_PIXEL_SHIFT_DISTANCE);
        assert(rect.y + rect.h <= size - DEFAULT_PIXEL_SHIFT_DISTANCE);
    }

    #if HAS_M5STACK_STOPWATCH
    const DisplayContentRect content = display_safe_content_rect(size, size);
    assert(content.x == 68 && content.y == 68 && content.w == 329 && content.h == 329);
    assert(rects[0].w == 61 && rects[0].h == 61);
    #else
    const PadGridLayoutSpace inscribed = {68, 68, 68, 68, 3, 3, 4};
    pad_compute_grid(5, 5, size, size, columns, rows, spans, spans, 25, rects, &inscribed);
    #endif
    for (const PadRect& rect : rects) {
        const int corners_x[] = {rect.x, rect.x + rect.w - 1};
        const int corners_y[] = {rect.y, rect.y + rect.h - 1};
        for (int corner_x : corners_x) {
            for (int corner_y : corners_y) {
                for (int shift_x : {-4, 4}) {
                    for (int shift_y : {-4, 4}) {
                        const int dx = 2 * (corner_x + shift_x) - (size - 1);
                        const int dy = 2 * (corner_y + shift_y) - (size - 1);
                        assert(dx * dx + dy * dy <= size * size);
                    }
                    #if HAS_M5STACK_STOPWATCH
                    // Runtime insets and a spanning button remain inside the same safe area
                    // used by the portal button_sizes route and native pad builder.
                    const PadGridLayoutSpace settings = {8, 12, 4, 16, 6, 6, 4};
                    columns[0] = rows[0] = 0;
                    spans[0] = 5;
                    pad_compute_grid(5, 5, size, size, columns, rows, spans, spans, 1, rects, &settings);
                    assert(rects[0].x == 80 && rects[0].y == 76);
                    assert(rects[0].x + rects[0].w <= content.x + content.w - 4 - settings.right);
                    assert(rects[0].y + rects[0].h <= content.y + content.h - 4 - settings.bottom);
                    #endif
                }
            }
        }
    }
}

int main() {
    test_layout_spacing_and_insets();
    test_stopwatch_466_square_round_viewport();
    std::puts("pad_layout: PASS");
    return 0;
}