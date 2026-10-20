#pragma once

#include <ArduinoJson.h>
#include <stdint.h>
#include "fill_pattern.h"

// Percent dimensions use 0 for Auto. Alignment/position: 0=start, 1=center, 2=end.
struct DialogAppearance {
    char font[32] = "";
    uint32_t text_color = 0xFFFFFF;
    uint32_t background_color = 0x333333;
    uint32_t background_pattern_color = 0x111111;
    uint32_t border_color = 0x808080;
    uint32_t approve_background = 0x2563EB;
    uint32_t approve_color = 0xFFFFFF;
    uint32_t cancel_background = 0x5F6368;
    uint32_t cancel_color = 0xFFFFFF;
    uint16_t font_size = 0;
    uint16_t button_height = 0;
    uint8_t background_opacity = 100;
    uint8_t backdrop_opacity = 60;
    uint8_t horizontal_align = 1;
    uint8_t vertical_align = 1;
    uint8_t border_width = 1;
    uint8_t corner_radius = 8;
    uint8_t padding = 16;
    uint8_t margin = 16;
    uint8_t width = 0;
    uint8_t height = 0;
    uint8_t position = 1;
    uint8_t button_width = 0;
    FillPattern background_pattern = FillPattern::None;
    uint8_t pattern_spacing = 8;
    uint8_t pattern_stroke_width = 1;
};

struct DialogTemplates {
    DialogAppearance notification;
    DialogAppearance confirmation;
    DialogTemplates() { notification.position = 2; }
};

void dialog_appearance_write(JsonObject root, const DialogTemplates& templates);
// Applies partial edits to a copy. No changes on validation failure.
const char* dialog_appearance_read(JsonObjectConst root, DialogTemplates& templates);
