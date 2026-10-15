#pragma once

#include "pad_layout.h"

static inline const lv_font_t* native_extension_text_font(const char* name, uint8_t size) {
    LabelStyle style{};
    style.font_size = size;
    if (name && *name && strcmp(name, "default") != 0) {
        if (strlen(name) >= sizeof(style.font_name)) return pad_nearest_bitmap_font(size ? size : 14);
        strcpy(style.font_name, name);
    }
    return pad_resolve_font(style, &lv_font_montserrat_14, &device_typography_fonts());
}

static inline int32_t native_extension_text_measure(const char* text, const lv_font_t* font) {
    if (!font || !text || !*text) return 0;
    int32_t width = 0;
    for (const uint8_t* cursor = reinterpret_cast<const uint8_t*>(text); *cursor; ++cursor) {
        lv_font_glyph_dsc_t glyph{};
        if (lv_font_get_glyph_dsc(font, &glyph, *cursor, cursor[1])) width += glyph.adv_w;
    }
    return width;
}

static inline void native_extension_text_draw(uint16_t* pixels, uint32_t width, uint32_t height,
                                               int32_t x, int32_t y, const char* text,
                                               const lv_font_t* font, lv_color_t color) {
    if (!pixels || !font || !text || !*text) return;
    lv_draw_buf_t* scratch = nullptr;
    int64_t cursor_x = x;
    for (const uint8_t* cursor = reinterpret_cast<const uint8_t*>(text); *cursor; ++cursor) {
        lv_font_glyph_dsc_t glyph{};
        if (!lv_font_get_glyph_dsc(font, &glyph, *cursor, cursor[1])) continue;
        const uint8_t* bitmap = nullptr;
        uint32_t stride = glyph.stride;
        uint8_t bpp = 0;
        if (glyph.box_w && glyph.box_h) {
            if (lv_font_has_static_bitmap(glyph.resolved_font)) {
                switch (glyph.format) {
                    case LV_FONT_GLYPH_FORMAT_A1: bpp = 1; break;
                    case LV_FONT_GLYPH_FORMAT_A2: bpp = 2; break;
                    case LV_FONT_GLYPH_FORMAT_A4: bpp = 4; break;
                    case LV_FONT_GLYPH_FORMAT_A8: bpp = 8; break;
                    default: break;
                }
                if (bpp) bitmap = static_cast<const uint8_t*>(lv_font_get_glyph_static_bitmap(&glyph));
            } else if (glyph.format >= LV_FONT_GLYPH_FORMAT_A1 && glyph.format <= LV_FONT_GLYPH_FORMAT_A8) {
                if (glyph.format != LV_FONT_GLYPH_FORMAT_A8) {
                    if (scratch && !lv_draw_buf_reshape(scratch, LV_COLOR_FORMAT_A8, glyph.box_w, glyph.box_h, LV_STRIDE_AUTO)) {
                        lv_draw_buf_destroy(scratch);
                        scratch = nullptr;
                    }
                    if (!scratch) scratch = lv_draw_buf_create(glyph.box_w, glyph.box_h, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
                }
                const auto* buffer = glyph.format == LV_FONT_GLYPH_FORMAT_A8 || scratch
                    ? static_cast<const lv_draw_buf_t*>(lv_font_get_glyph_bitmap(&glyph, scratch)) : nullptr;
                if (buffer && buffer->header.cf == LV_COLOR_FORMAT_A8 &&
                    buffer->header.w >= glyph.box_w && buffer->header.h >= glyph.box_h &&
                    buffer->header.stride >= glyph.box_w) {
                    bitmap = buffer->data;
                    stride = buffer->header.stride;
                    bpp = 8;
                }
            }
        }
        if (bitmap) {
            const int64_t left = cursor_x + glyph.ofs_x;
            const int64_t top = static_cast<int64_t>(y) + font->line_height - font->base_line -
                glyph.box_h - glyph.ofs_y;
            const uint32_t mask = (1u << bpp) - 1u;
            for (uint32_t row = 0; row < glyph.box_h; ++row) {
                const int64_t py = top + row;
                if (py < 0 || py >= height) continue;
                for (uint32_t column = 0; column < glyph.box_w; ++column) {
                    const int64_t px = left + column;
                    if (px < 0 || px >= width) continue;
                    const size_t bit_index = stride ? static_cast<size_t>(row) * stride * 8 + column * bpp
                        : (static_cast<size_t>(row) * glyph.box_w + column) * bpp;
                    const uint8_t coverage = ((bitmap[bit_index / 8] >> (8 - bpp - bit_index % 8)) & mask) * 255u / mask;
                    if (!coverage) continue;
                    uint16_t& destination = pixels[static_cast<size_t>(py) * width + static_cast<size_t>(px)];
                    const lv_color_t background = lv_color_make(
                        static_cast<uint8_t>(((destination >> 11) & 0x1Fu) * 255u / 31u),
                        static_cast<uint8_t>(((destination >> 5) & 0x3Fu) * 255u / 63u),
                        static_cast<uint8_t>((destination & 0x1Fu) * 255u / 31u));
                    destination = lv_color_to_u16(lv_color_mix(color, background, coverage));
                }
            }
        }
        cursor_x += glyph.adv_w;
        lv_font_glyph_release_draw_data(&glyph);
    }
    if (scratch) lv_draw_buf_destroy(scratch);
}