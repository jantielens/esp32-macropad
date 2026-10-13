#include "pad_config.h"
#include "../src/app/fill_pattern.h"

#include <assert.h>
#include <stdio.h>

int main() {
    // PadConfig stores a pointer to only the configured buttons. A fixed
    // ScreenButtonConfig[MAX_PAD_BUTTONS] member would exceed this bound.
    static_assert(sizeof(PadConfig) < 16 * 1024,
                  "PadConfig must not embed the maximum button array");

    PadConfig config = {};
    assert(config.buttons == nullptr);
    assert(config.button_capacity == 0);
    assert(fill_pattern_parse(nullptr) == FillPattern::None);
    assert(fill_pattern_parse("unknown") == FillPattern::None);
    assert(fill_pattern_parse("none") == FillPattern::None);
    assert(fill_pattern_parse("dots") == FillPattern::Dots);
    for (unsigned pattern = 0; pattern < 6; ++pattern) {
        for (int vertical = -9; vertical < 9; ++vertical) {
            for (int horizontal = -9; horizontal < 9; ++horizontal) {
                FillPattern selected = static_cast<FillPattern>(pattern);
                bool marked = fill_pattern_pixel(selected, horizontal, vertical, 8, 1);
                assert(marked == fill_pattern_pixel(selected, horizontal + 16, vertical + 16, 8, 1));
            }
        }
    }
    assert(!fill_pattern_pixel(FillPattern::None, 0, 0, 8, 1));
    assert(fill_pattern_pixel(FillPattern::Diagonal, 0, 0, 8, 1));
    assert(!fill_pattern_pixel(FillPattern::Diagonal, 1, 0, 8, 1));
    assert(fill_pattern_pixel(FillPattern::Dots, 0, 0, 8, 1));
    assert(!fill_pattern_pixel(FillPattern::Dots, 1, 0, 8, 1));
    assert(fill_pattern_pixel(FillPattern::Checkerboard, 0, 8, 8, 1));
    assert(!fill_pattern_pixel(FillPattern::Checkerboard, 0, 0, 8, 1));
    puts("pad_config_dynamic_layout: PASS");
    return 0;
}