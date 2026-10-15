#ifndef FILL_PATTERN_H
#define FILL_PATTERN_H

#include <stdint.h>
#include <string.h>

enum class FillPattern : uint8_t { None, Diagonal, ReverseDiagonal, Crosshatch, Dots, Checkerboard };

struct ButtonFillStyle {
    uint16_t bytes;
    uint16_t color_offsets[7];
    FillPattern patterns[6];
    uint8_t spacing;
    uint8_t stroke;
    uint8_t outline_width;
    uint8_t track_radius;

    const char* color(unsigned index) const {
        return reinterpret_cast<const char*>(this) + color_offsets[index];
    }
};

inline int fill_geometry_clamp(int value, int minimum, int maximum) {
    return value < minimum ? minimum : value > maximum ? maximum : value;
}

inline FillPattern fill_pattern_parse(const char* name) {
    const char* names[] = {"none", "diagonal", "reverse_diagonal", "crosshatch", "dots", "checkerboard"};
    for (unsigned index = 0; index < 6; ++index) {
        if (name && strcmp(name, names[index]) == 0) return static_cast<FillPattern>(index);
    }
    return FillPattern::None;
}

inline int fill_pattern_mod(int value, int spacing) {
    int result = value % spacing;
    return result < 0 ? result + spacing : result;
}

inline bool fill_pattern_pixel(FillPattern pattern, int horizontal, int vertical,
                               int spacing, int stroke) {
    if (spacing < 2) spacing = 2;
    if (spacing > 64) spacing = 64;
    if (stroke < 1) stroke = 1;
    if (stroke > 8) stroke = 8;
    if (stroke > spacing) stroke = spacing;
    switch (pattern) {
        case FillPattern::Diagonal: return fill_pattern_mod(horizontal + vertical, spacing) < stroke;
        case FillPattern::ReverseDiagonal: return fill_pattern_mod(horizontal - vertical, spacing) < stroke;
        case FillPattern::Crosshatch:
            return fill_pattern_mod(horizontal + vertical, spacing) < stroke ||
                   fill_pattern_mod(horizontal - vertical, spacing) < stroke;
        case FillPattern::Dots: {
            int delta_x = 2 * fill_pattern_mod(horizontal, spacing) - stroke + 1;
            int delta_y = 2 * fill_pattern_mod(vertical, spacing) - stroke + 1;
            return delta_x * delta_x + delta_y * delta_y <= stroke * stroke;
        }
        case FillPattern::Checkerboard:
            return (fill_pattern_mod(horizontal, 2 * spacing) < spacing) !=
                   (fill_pattern_mod(vertical, 2 * spacing) < spacing);
        default: return false;
    }
}

#endif