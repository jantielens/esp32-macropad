#ifndef PAD_LAYOUT_H
#define PAD_LAYOUT_H

#include "board_config.h"
#include "pad_config.h"
#include "display_geometry.h"
#include <lvgl.h>
#include "pad_font_cache.h"
#include "pad_track_layout.h"
#include <stdint.h>
#include <string.h>

// ============================================================================
// Pad Layout Engine
// ============================================================================
// Pure-computation layout engine: display dimensions + grid config → tile rects.
// No LVGL object creation — just math. Used by PadScreen to position tiles.

// ============================================================================
// UI Scale Table
// ============================================================================
struct UIScaleInfo {
    const lv_font_t* font_small;
    const lv_font_t* font_medium;
    const lv_font_t* font_large;
    uint8_t gap;
    uint8_t icon_height_pct;  // % of tile_h for icon sizing
};

static const UIScaleInfo UI_SCALE_TABLE[] = {
    // SMALL:  12, 14, 18, gap=2, icon=40%
    { &lv_font_montserrat_12, &lv_font_montserrat_14, &lv_font_montserrat_18, 2, 40 },
    // MEDIUM: 14, 18, 24, gap=3, icon=45%
    { &lv_font_montserrat_14, &lv_font_montserrat_18, &lv_font_montserrat_24, 3, 45 },
    // LARGE:  14, 24, 32, gap=3, icon=45%
    { &lv_font_montserrat_14, &lv_font_montserrat_24, &lv_font_montserrat_32, 3, 45 },
    // XLARGE: 18, 24, 36, gap=4, icon=50%
    { &lv_font_montserrat_18, &lv_font_montserrat_24, &lv_font_montserrat_36, 4, 50 },
};

// Convenience accessor for current board's scale info
static inline const UIScaleInfo& pad_get_scale_info() {
    return UI_SCALE_TABLE[UI_SCALE_TIER];
}

// ============================================================================
// Label Style Helpers — map LabelStyle values to LVGL constants
// ============================================================================

// Look up a compiled Montserrat font by pixel size. Returns nullptr if not compiled in.
static inline const lv_font_t* pad_font_by_size(uint16_t size) {
    switch (size) {
        case 12: return &lv_font_montserrat_12;
        case 14: return &lv_font_montserrat_14;
        case 18: return &lv_font_montserrat_18;
        case 24: return &lv_font_montserrat_24;
        case 32: return &lv_font_montserrat_32;
        case 36: return &lv_font_montserrat_36;
        default: return nullptr;
    }
}

static inline uint16_t pad_font_pixel_size(const lv_font_t* font) {
    const uint16_t sizes[] = {12, 14, 18, 24, 32, 36};
    for (uint16_t size : sizes) if (pad_font_by_size(size) == font) return size;
    return font ? lv_font_get_line_height(font) : 14;
}

static inline const lv_font_t* pad_nearest_bitmap_font(uint32_t size, uint32_t denominator = 1) {
    const uint16_t sizes[] = {12, 14, 18, 24, 32, 36};
    uint16_t nearest = 12;
    uint32_t distance = size > 12 * denominator ? size - 12 * denominator : 12 * denominator - size;
    for (uint16_t candidate : sizes) {
        const uint32_t value = candidate * denominator;
        const uint32_t delta = size > value ? size - value : value - size;
        if (delta <= distance) { nearest = candidate; distance = delta; }
    }
    return pad_font_by_size(nearest);
}

static inline const lv_font_t* pad_resolve_font(const LabelStyle& style, const lv_font_t* default_font,
                                               PadFontCache* owner = nullptr) {
    const DeviceTypography& defaults = device_typography_get();
    const uint32_t medium = pad_font_pixel_size(pad_get_scale_info().font_medium);
    const uint32_t original = pad_font_pixel_size(default_font);
    const uint32_t denominator = !style.font_size && defaults.base_size ? medium : 1;
    const uint32_t numerator = style.font_size ? style.font_size : defaults.base_size
        ? original * defaults.base_size : original;
    const uint32_t rounded = (numerator + denominator / 2) / denominator;
    const uint32_t size = rounded ? rounded : 1;
    const lv_font_t* fallback = (!style.font_size && !defaults.base_size)
        ? default_font : pad_nearest_bitmap_font(numerator, denominator);
    const char* name = style.font_name[0] ? style.font_name : defaults.font_name;
    if (!name[0] || strcmp(name, "@builtin") == 0 || !size || size > UINT16_MAX) return fallback;
    PadFontCache* cache = style.font_name[0] ? owner : &device_typography_fonts();
    return cache ? cache->resolve(name, static_cast<uint16_t>(size), fallback, style.font_metrics) : fallback;
}

static inline const lv_font_t* pad_typography_font(const lv_font_t* original) {
    return pad_resolve_font(LabelStyle{}, original);
}

static inline bool pad_is_bitmap_font(const lv_font_t* font) {
    const uint16_t sizes[] = {12, 14, 18, 24, 32, 36};
    for (uint16_t size : sizes) if (font == pad_font_by_size(size)) return true;
    return false;
}

enum PadLabelAnchorY : uint8_t {
    PAD_LABEL_ANCHOR_TOP = 0,
    PAD_LABEL_ANCHOR_CENTER = 1,
    PAD_LABEL_ANCHOR_BOTTOM = 2,
};

// Apply optional runtime text upscale for hero-style labels.
// Uses anchor-aware transform pivot to keep labels visually pinned in place.
static inline void pad_apply_font_upscale(lv_obj_t* lbl, const LabelStyle& style, PadLabelAnchorY anchor_y) {
    if (style.font_upscale == 0 || !pad_is_bitmap_font(lv_obj_get_style_text_font(lbl, LV_PART_MAIN))) return;

    lv_obj_update_layout(lbl);
    const lv_coord_t w = lv_obj_get_width(lbl);
    lv_coord_t h = lv_obj_get_height(lbl);
    if (h <= 0) {
        const lv_font_t* font = lv_obj_get_style_text_font(lbl, LV_PART_MAIN);
        if (font) h = lv_font_get_line_height(font);
    }

    lv_coord_t pivot_y = h / 2;
    if (anchor_y == PAD_LABEL_ANCHOR_TOP) {
        pivot_y = 0;
    } else if (anchor_y == PAD_LABEL_ANCHOR_BOTTOM) {
        pivot_y = h;
    }

    lv_obj_set_style_transform_pivot_x(lbl, w / 2, 0);
    lv_obj_set_style_transform_pivot_y(lbl, pivot_y, 0);
    lv_obj_set_style_transform_scale_x(lbl, style.font_upscale, 0);
    lv_obj_set_style_transform_scale_y(lbl, style.font_upscale, 0);
}

// Resolve LVGL text alignment from LabelStyle.
static inline lv_text_align_t pad_resolve_align(const LabelStyle& style) {
    switch (style.align) {
        case LABEL_ALIGN_LEFT:   return LV_TEXT_ALIGN_LEFT;
        case LABEL_ALIGN_RIGHT:  return LV_TEXT_ALIGN_RIGHT;
        case LABEL_ALIGN_CENTER: return LV_TEXT_ALIGN_CENTER;
        default:                 return LV_TEXT_ALIGN_CENTER;
    }
}

// Resolve LVGL long mode from LabelStyle.
static inline lv_label_long_mode_t pad_resolve_long_mode(const LabelStyle& style) {
    switch (style.long_mode) {
        case LABEL_MODE_CLIP:   return LV_LABEL_LONG_CLIP;
        case LABEL_MODE_SCROLL: return LV_LABEL_LONG_SCROLL_CIRCULAR;
        case LABEL_MODE_DOT:    return LV_LABEL_LONG_DOT;
        case LABEL_MODE_WRAP:   return LV_LABEL_LONG_WRAP;
        default:                return LV_LABEL_LONG_CLIP;
    }
}

// Apply long mode to a label. For DOT mode, also constrain label height to one
// line so LVGL can detect vertical overflow and show the "..." ellipsis.
static inline void pad_apply_long_mode(lv_obj_t* lbl, const LabelStyle& style) {
    lv_label_long_mode_t mode = pad_resolve_long_mode(style);
    if (mode == LV_LABEL_LONG_DOT) {
        const lv_font_t* font = lv_obj_get_style_text_font(lbl, LV_PART_MAIN);
        lv_obj_set_height(lbl, lv_font_get_line_height(font));
    }
    lv_label_set_long_mode(lbl, mode);
}

// Resolve label color: if style has a color override (marker bit set), use it; otherwise use fg.
static inline lv_color_t pad_resolve_label_color(const LabelStyle& style, lv_color_t fg) {
    if (style.color & 0x01000000) {
        return lv_color_make((style.color >> 16) & 0xFF,
                             (style.color >> 8) & 0xFF,
                             style.color & 0xFF);
    }
    return fg;
}

// ============================================================================
// PadRect — output tile rectangle
// ============================================================================
struct PadRect {
    int16_t x, y;
    uint16_t w, h;
};

// Device-wide pad layout space. Edge insets sit inside the pixel-shift margin.
struct PadGridLayoutSpace {
    uint16_t left;
    uint16_t right;
    uint16_t top;
    uint16_t bottom;
    uint16_t gap_x;
    uint16_t gap_y;
    uint16_t pixel_shift_margin;
};

// ============================================================================
// Grid Computation
// ============================================================================
// Compute tile rectangles for a grid layout.
//
// Inputs:
//   cols, rows       — grid dimensions (1-8 each)
//   display_w/h      — full display resolution
//   button_cols[]    — per-button column positions (0-based)
//   button_rows[]    — per-button row positions (0-based)
//   button_col_spans[] — per-button column spans (1+)
//   button_row_spans[] — per-button row spans (1+)
//   button_count     — number of buttons
//
// Output:
//   out_rects[]      — computed rectangles (caller must allocate button_count entries)

static inline void pad_compute_grid(
    uint8_t cols, uint8_t rows,
    uint16_t display_w, uint16_t display_h,
    const uint8_t* button_cols, const uint8_t* button_rows,
    const uint8_t* button_col_spans, const uint8_t* button_row_spans,
    uint8_t button_count,
    PadRect* out_rects,
    const PadGridLayoutSpace* layout_space = nullptr,
    const uint16_t* column_widths = nullptr,
    const uint16_t* row_heights = nullptr)
{
    const uint16_t default_gap = pad_get_scale_info().gap;
    const uint16_t left = layout_space ? layout_space->left : 0;
    const uint16_t right = layout_space ? layout_space->right : 0;
    const uint16_t top = layout_space ? layout_space->top : 0;
    const uint16_t bottom = layout_space ? layout_space->bottom : 0;
    const uint16_t gap_x = layout_space ? layout_space->gap_x : default_gap;
    const uint16_t gap_y = layout_space ? layout_space->gap_y : default_gap;
    const uint16_t pixel_shift_margin = layout_space
        ? layout_space->pixel_shift_margin : DEFAULT_PIXEL_SHIFT_DISTANCE;
    const uint16_t inset_w = 2 * pixel_shift_margin + left + right;
    const uint16_t inset_h = 2 * pixel_shift_margin + top + bottom;
    const DisplayContentRect content = display_safe_content_rect(display_w, display_h);
    const uint16_t safe_w = content.w > inset_w ? content.w - inset_w : 0;
    const uint16_t safe_h = content.h > inset_h ? content.h - inset_h : 0;

    uint16_t column_sizes[MAX_GRID_COLS] = {};
    uint16_t row_sizes[MAX_GRID_ROWS] = {};
    pad_compute_axis_track_sizes(cols, safe_w, gap_x, column_widths, column_sizes);
    pad_compute_axis_track_sizes(rows, safe_h, gap_y, row_heights, row_sizes);

    for (uint8_t i = 0; i < button_count; i++) {
        const uint8_t c = button_cols[i];
        const uint8_t r = button_rows[i];
        const uint8_t cs = button_col_spans[i] > 0 ? button_col_spans[i] : 1;
        const uint8_t rs = button_row_spans[i] > 0 ? button_row_spans[i] : 1;

        uint32_t x_offset = 0;
        uint32_t y_offset = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        for (uint16_t track = 0; track < c; track++)
            x_offset += (track < cols ? column_sizes[track] : column_sizes[cols - 1]) + gap_x;
        for (uint16_t track = 0; track < r; track++)
            y_offset += (track < rows ? row_sizes[track] : row_sizes[rows - 1]) + gap_y;
        for (uint8_t span = 0; span < cs; span++) {
            const uint8_t track = c + span;
            width += track < cols ? column_sizes[track] : column_sizes[cols - 1];
            if (span + 1 < cs) width += gap_x;
        }
        for (uint8_t span = 0; span < rs; span++) {
            const uint8_t track = r + span;
            height += track < rows ? row_sizes[track] : row_sizes[rows - 1];
            if (span + 1 < rs) height += gap_y;
        }
        out_rects[i].x = content.x + pixel_shift_margin + left + x_offset;
        out_rects[i].y = content.y + pixel_shift_margin + top + y_offset;
        out_rects[i].w = width;
        out_rects[i].h = height;
    }
}

#endif // PAD_LAYOUT_H
