#include "board_config.h"

#if HAS_DISPLAY
#include "dialog_appearance.h"
#include "pad_config.h"
#include <string.h>
#include <stdio.h>

namespace {
#define DIALOG_NUMBERS(X) \
    X(font_size, 0, 96, 8) \
    X(background_opacity, 0, 100, 0) X(backdrop_opacity, 0, 100, 0) \
    X(horizontal_align, 0, 2, 0) X(vertical_align, 0, 2, 0) \
    X(border_width, 0, 10, 0) X(corner_radius, 0, 40, 0) \
    X(padding, 0, 48, 0) X(margin, 0, 48, 0) \
    X(width, 0, 100, 20) X(height, 0, 100, 20) X(position, 0, 2, 0)
#define DIALOG_BUTTON_NUMBERS(X) X(button_width, 0, 50, 10) X(button_height, 0, 160, 44)
#define DIALOG_COLORS(X) X(text_color) X(background_color) X(border_color) X(background_pattern_color)
#define DIALOG_BUTTON_COLORS(X) X(approve_background) X(approve_color) X(cancel_background) X(cancel_color)

void write_template(JsonObject object, const DialogAppearance& style, bool confirmation) {
    object["font"] = style.font;
    object["background_pattern"] = fill_pattern_name(style.background_pattern);
    object["pattern_spacing"] = style.pattern_spacing;
    object["pattern_stroke_width"] = style.pattern_stroke_width;
#define WRITE_NUMBER(name, min, max, nonzero_min) object[#name] = style.name;
    DIALOG_NUMBERS(WRITE_NUMBER)
    if (confirmation) { DIALOG_BUTTON_NUMBERS(WRITE_NUMBER) }
#undef WRITE_NUMBER
#define WRITE_COLOR(name) { char color[8]; snprintf(color, sizeof(color), "#%06lX", static_cast<unsigned long>(style.name & 0xFFFFFF)); object[#name] = color; }
    DIALOG_COLORS(WRITE_COLOR)
    if (confirmation) { DIALOG_BUTTON_COLORS(WRITE_COLOR) }
#undef WRITE_COLOR
}

const char* read_template(JsonObjectConst object, DialogAppearance& style, bool confirmation) {
    if (object.containsKey("background_pattern")) {
        if (!object["background_pattern"].is<const char*>()) return "dialog background_pattern must be a literal pattern name";
        const char* name = object["background_pattern"];
        const FillPattern pattern = fill_pattern_parse(name);
        if (strcmp(name, fill_pattern_name(pattern))) return "invalid dialog background_pattern";
        style.background_pattern = pattern;
    }
    if (object.containsKey("pattern_spacing")) {
        if (!object["pattern_spacing"].is<unsigned>() || object["pattern_spacing"].as<unsigned>() < 2 ||
            object["pattern_spacing"].as<unsigned>() > 64) return "dialog pattern_spacing must be 2-64 px";
        style.pattern_spacing = object["pattern_spacing"].as<unsigned>();
    }
    if (object.containsKey("pattern_stroke_width")) {
        if (!object["pattern_stroke_width"].is<unsigned>() || object["pattern_stroke_width"].as<unsigned>() < 1 ||
            object["pattern_stroke_width"].as<unsigned>() > 8) return "dialog pattern_stroke_width must be 1-8 px";
        style.pattern_stroke_width = object["pattern_stroke_width"].as<unsigned>();
    }
    if (style.pattern_stroke_width > style.pattern_spacing) return "dialog pattern thickness must not exceed spacing";
    if (object.containsKey("font")) {
        if (!object["font"].is<const char*>()) return "dialog font must be a literal font identifier";
        const char* font = object["font"];
        if (strlen(font) >= sizeof(style.font)) return "dialog font identifier too long";
        if (strcmp(font, "@builtin") != 0) {
            for (const char* p = font; *p; ++p) {
                if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                      (*p >= '0' && *p <= '9') || *p == '_' || *p == '-'))
                    return "invalid dialog font identifier";
            }
        }
        strlcpy(style.font, font, sizeof(style.font));
    }
#define READ_NUMBER(name, min, max, nonzero_min) \
    if (object.containsKey(#name)) { \
        if (!object[#name].is<unsigned>() || object[#name].as<unsigned>() > max || \
            (object[#name].as<int>() != 0 && object[#name].as<int>() < nonzero_min)) \
            return "invalid dialog " #name; \
        style.name = object[#name].as<unsigned>(); \
    }
    DIALOG_NUMBERS(READ_NUMBER)
    if (confirmation) { DIALOG_BUTTON_NUMBERS(READ_NUMBER) }
#undef READ_NUMBER
#define READ_COLOR(name) \
    if (object.containsKey(#name) && (!object[#name].is<const char*>() || \
        !parse_hex_color(object[#name].as<const char*>(), &style.name))) return "invalid dialog " #name;
    DIALOG_COLORS(READ_COLOR)
    if (confirmation) { DIALOG_BUTTON_COLORS(READ_COLOR) }
#undef READ_COLOR
    if (!strcmp(style.font, "@builtin") && style.font_size &&
        style.font_size != 12 && style.font_size != 14 && style.font_size != 18 &&
        style.font_size != 24 && style.font_size != 32 && style.font_size != 36)
        return "choose an available bitmap font size or Automatic";
    return nullptr;
}
}

void dialog_appearance_write(JsonObject root, const DialogTemplates& templates) {
    write_template(root["notification"].to<JsonObject>(), templates.notification, false);
    write_template(root["confirmation"].to<JsonObject>(), templates.confirmation, true);
}

const char* dialog_appearance_read(JsonObjectConst root, DialogTemplates& templates) {
    DialogTemplates candidate = templates;
    for (const char* name : {"notification", "confirmation"}) {
        if (!root.containsKey(name)) continue;
        if (!root[name].is<JsonObjectConst>()) return "dialog appearance templates must be objects";
        const bool confirmation = !strcmp(name, "confirmation");
        const char* error = read_template(root[name].as<JsonObjectConst>(),
            confirmation ? candidate.confirmation : candidate.notification, confirmation);
        if (error) return error;
    }
    templates = candidate;
    return nullptr;
}
#endif
