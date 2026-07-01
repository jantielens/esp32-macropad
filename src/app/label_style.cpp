#include "pad_config.h"

#if HAS_DISPLAY

#include <stdlib.h>
#include <string.h>

// ============================================================================
// Label Style DSL parser
// ============================================================================
// Format: "key:value;key:value;..."
// Keys: font (12/14/18/24/32/36/48), font_upscale (1.0..2.0),
//        align (left/center/right), x/y (int offset),
//        mode (clip/scroll/dot/wrap), color (#RRGGBB or a [scheme:...] binding)
//
// Extracted from pad_config.cpp so it can be exercised by host-native tests
// (it depends only on parse_hex_color from the header and libc string helpers).
void label_style_parse(const char* dsl, LabelStyle* out,
                       char* color_bind_out, size_t color_bind_len) {
    memset(out, 0, sizeof(LabelStyle));
    if (color_bind_out && color_bind_len) color_bind_out[0] = '\0';
    if (!dsl || !dsl[0]) return;

    // Work on a local copy to tokenize
    char buf[CONFIG_LABEL_STYLE_MAX_LEN];
    strlcpy(buf, dsl, sizeof(buf));

    // Split on ';' only at bracket-depth 0 and outside quotes, so a binding-valued
    // color (e.g. color:[expr:cond?"#aa":"#bb";x]) survives nested [] and quoted
    // ';'/':'. Mirrors split_pipe_fallback() in binding_template.cpp.
    char* cursor = buf;
    while (cursor && *cursor) {
        char* token = cursor;
        bool in_quotes = false;
        int bracket_depth = 0;
        char* p = cursor;
        for (; *p; p++) {
            if (*p == '"') in_quotes = !in_quotes;
            else if (!in_quotes) {
                if (*p == '[') bracket_depth++;
                else if (*p == ']') { if (bracket_depth > 0) bracket_depth--; }
                else if (*p == ';' && bracket_depth == 0) break;
            }
        }
        if (*p == ';') { *p = '\0'; cursor = p + 1; }
        else            cursor = nullptr; // last token
        // Skip leading whitespace
        while (*token == ' ') token++;
        char* colon = strchr(token, ':');
        if (colon) {
            *colon = '\0';
            const char* key = token;
            const char* val = colon + 1;

            if (strcmp(key, "font_size") == 0 || strcmp(key, "font") == 0) {
                int sz = atoi(val);
                if (sz == 12 || sz == 14 || sz == 18 || sz == 24 || sz == 32 || sz == 36 || sz == 48) {
                    out->font_size = (uint8_t)sz;
                }
            } else if (strcmp(key, "font_family") == 0) {
                if (strcmp(val, "segment") == 0 || strcmp(val, "dseg7") == 0)  out->font_family = 1;
                else if (strcmp(val, "bebas") == 0)                           out->font_family = 2;
                else if (strcmp(val, "doto") == 0 || strcmp(val, "pixel") == 0) out->font_family = 3;
            } else if (strcmp(key, "font_upscale") == 0) {
                char* end = nullptr;
                double f = strtod(val, &end);
                if (end != val) {
                    if (f < 1.0) f = 1.0;
                    if (f > 2.0) f = 2.0;
                    // Store using LVGL transform scale units where 256 == 1.0x.
                    out->font_upscale = (uint16_t)(f * 256.0 + 0.5);
                    if (out->font_upscale <= 256) out->font_upscale = 0;
                }
            } else if (strcmp(key, "align") == 0) {
                if (strcmp(val, "left") == 0)        out->align = LABEL_ALIGN_LEFT;
                else if (strcmp(val, "right") == 0)  out->align = LABEL_ALIGN_RIGHT;
                else if (strcmp(val, "center") == 0) out->align = LABEL_ALIGN_CENTER;
            } else if (strcmp(key, "x") == 0) {
                int x = atoi(val);
                if (x < -999) x = -999;
                if (x > 999)  x = 999;
                out->x_offset = (int16_t)x;
            } else if (strcmp(key, "y") == 0) {
                int y = atoi(val);
                if (y < -999) y = -999;
                if (y > 999)  y = 999;
                out->y_offset = (int16_t)y;
            } else if (strcmp(key, "mode") == 0) {
                if (strcmp(val, "clip") == 0)        out->long_mode = LABEL_MODE_CLIP;
                else if (strcmp(val, "scroll") == 0) out->long_mode = LABEL_MODE_SCROLL;
                else if (strcmp(val, "dot") == 0)    out->long_mode = LABEL_MODE_DOT;
                else if (strcmp(val, "wrap") == 0)   out->long_mode = LABEL_MODE_WRAP;
            } else if (strcmp(key, "color") == 0) {
                uint32_t c;
                if (parse_hex_color(val, &c)) {
                    out->color = c | 0x01000000; // Set marker bit
                } else if (strchr(val, '[') && color_bind_out && color_bind_len) {
                    // Binding-valued color: capture the raw token for live runtime
                    // resolution; leave out->color unset (marker bit clear).
                    strlcpy(color_bind_out, val, color_bind_len);
                }
            }
        }
    }
}

#endif // HAS_DISPLAY
