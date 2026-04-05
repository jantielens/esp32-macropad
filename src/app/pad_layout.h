#pragma once

#include "board_config.h"
#include "pad_config.h"
#if HAS_CUSTOM_FONTS
#include "fonts/custom_fonts.h"
#endif
#include <lvgl.h>
#include <stdint.h>

// ============================================================================
// Pad Layout Engine
// ============================================================================
// Pure-computation layout engine: display dimensions + grid config → tile rects.
// No LVGL object creation — just math. Used by PadScreen to position tiles.

// Pixel shift margin reserved on all sides (matches screen saver ±4px range)
#define PIXEL_SHIFT_MARGIN 4

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
static inline const lv_font_t* pad_font_by_size(uint8_t size) {
    switch (size) {
        case 12: return &lv_font_montserrat_12;
        case 14: return &lv_font_montserrat_14;
        case 18: return &lv_font_montserrat_18;
        case 24: return &lv_font_montserrat_24;
        case 32: return &lv_font_montserrat_32;
        case 36: return &lv_font_montserrat_36;
        case 48: return &lv_font_montserrat_48;
        default: return nullptr;
    }
}

// Resolve font for a label: use style override if set, otherwise the provided default.
// When font_family is set and HAS_CUSTOM_FONTS is enabled, dispatches to custom fonts.
// When only font_size is set, uses the built-in Montserrat font at that size.
static inline const lv_font_t* pad_resolve_font(const LabelStyle& style, const lv_font_t* default_font) {
#if HAS_CUSTOM_FONTS
    if (style.font_family != 0) {
        // Custom font family requested — use custom font lookup with requested or default size
        uint8_t sz = style.font_size ? style.font_size : lv_font_get_line_height(default_font);
        const lv_font_t* f = pad_custom_font_lookup(style.font_family, sz);
        if (f) return f;
        // Fall through to Montserrat if custom family not found
    }
#endif
    if (style.font_size) {
        const lv_font_t* f = pad_font_by_size(style.font_size);
        if (f) return f;
    }
    return default_font;
}

enum PadLabelAnchorY : uint8_t {
    PAD_LABEL_ANCHOR_TOP = 0,
    PAD_LABEL_ANCHOR_CENTER = 1,
    PAD_LABEL_ANCHOR_BOTTOM = 2,
};

// Apply optional runtime text upscale for hero-style labels.
// Uses anchor-aware transform pivot to keep labels visually pinned in place.
static inline void pad_apply_font_upscale(lv_obj_t* lbl, const LabelStyle& style, PadLabelAnchorY anchor_y) {
    if (style.font_upscale == 0) return;

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
    PadRect* out_rects)
{
    const uint8_t gap = pad_get_scale_info().gap;
    const uint16_t safe_w = display_w - 2 * PIXEL_SHIFT_MARGIN;
    const uint16_t safe_h = display_h - 2 * PIXEL_SHIFT_MARGIN;

    const uint16_t tile_w = (safe_w - (cols - 1) * gap) / cols;
    const uint16_t tile_h = (safe_h - (rows - 1) * gap) / rows;

    for (uint8_t i = 0; i < button_count; i++) {
        const uint8_t c = button_cols[i];
        const uint8_t r = button_rows[i];
        const uint8_t cs = button_col_spans[i] > 0 ? button_col_spans[i] : 1;
        const uint8_t rs = button_row_spans[i] > 0 ? button_row_spans[i] : 1;

        out_rects[i].x = PIXEL_SHIFT_MARGIN + c * (tile_w + gap);
        out_rects[i].y = PIXEL_SHIFT_MARGIN + r * (tile_h + gap);
        out_rects[i].w = cs * tile_w + (cs - 1) * gap;
        out_rects[i].h = rs * tile_h + (rs - 1) * gap;
    }
}
