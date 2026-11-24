#include "native_extension_api.h"
#include "calculator_logic.h"

extern "C" const NativeExtensionDescriptor native_extension_descriptor = {
    NATIVE_EXTENSION_DESCRIPTOR_MAGIC, NATIVE_EXTENSION_ABI_VERSION,
    NATIVE_EXTENSION_TARGET_ABI, "calculator", "1.1.0", "Calculator",
    NATIVE_EXTENSION_TICK_INTERVAL_DEFAULT_MS, 0,
};

namespace {

struct Instance;
struct KeyBinding {
    NativeExtensionUiEventBinding event;
    Instance* instance;
    calculator::Key key;
};

struct Instance {
    Instance* next;
    const NativeExtensionHostApi* host;
    void* extension_context;
    uint32_t id;
    void* panel;
    void* display;
    calculator::State calculator;
    KeyBinding keys[24];
    char displayed[32];
    int32_t display_width;
    int32_t display_height;
    char font[32];
    char display_font[32];
    NativeExtensionAlign display_alignment;
};

struct Colors {
    uint32_t background;
    uint32_t foreground;
    bool has_background;
    bool has_foreground;
};

struct Style {
    bool epaper;
    int32_t radius;
    char font[32];
    Colors defaults;
    Colors categories[4];
    Colors display;
    uint32_t display_border_color;
    bool has_display_border_color;
    int32_t display_radius;
    char display_font[32];
    NativeExtensionAlign display_alignment;
};

bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
void skip_space(const char*& p) { while (whitespace(*p)) ++p; }
bool consume(const char*& p, const char* text) {
    const char* cursor = p;
    while (*text && *cursor == *text) { ++cursor; ++text; }
    if (*text) return false;
    p = cursor;
    return true;
}

bool same_text(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool append_byte(char* out, size_t capacity, size_t* length, uint8_t byte) {
    if (!byte || *length + 1 >= capacity) return false;
    out[(*length)++] = static_cast<char>(byte);
    return true;
}

bool unicode_escape(const char*& p, uint32_t* codepoint) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (!*p) return false;
        const int digit = hex_digit(*p++);
        if (digit < 0) return false;
        value = (value << 4) | static_cast<uint32_t>(digit);
    }
    *codepoint = value;
    return true;
}

bool json_string(const char*& p, char* out, size_t capacity) {
    if (*p++ != '"') return false;
    size_t length = 0;
    while (*p && *p != '"') {
        unsigned char ch = static_cast<unsigned char>(*p++);
        if (ch < 0x20) return false;
        if (ch == '\\') {
            ch = static_cast<unsigned char>(*p++);
            if (ch == 'u') {
                uint32_t codepoint;
                if (!unicode_escape(p, &codepoint)) return false;
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (*p++ != '\\' || *p++ != 'u') return false;
                    uint32_t low;
                    if (!unicode_escape(p, &low) || low < 0xdc00 || low > 0xdfff) return false;
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + low - 0xdc00;
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) return false;
                if (codepoint < 0x80) {
                    if (!append_byte(out, capacity, &length, static_cast<uint8_t>(codepoint))) return false;
                } else if (codepoint < 0x800) {
                    if (!append_byte(out, capacity, &length, 0xc0 | (codepoint >> 6)) ||
                        !append_byte(out, capacity, &length, 0x80 | (codepoint & 0x3f))) return false;
                } else if (codepoint < 0x10000) {
                    if (!append_byte(out, capacity, &length, 0xe0 | (codepoint >> 12)) ||
                        !append_byte(out, capacity, &length, 0x80 | ((codepoint >> 6) & 0x3f)) ||
                        !append_byte(out, capacity, &length, 0x80 | (codepoint & 0x3f))) return false;
                } else {
                    if (!append_byte(out, capacity, &length, 0xf0 | (codepoint >> 18)) ||
                        !append_byte(out, capacity, &length, 0x80 | ((codepoint >> 12) & 0x3f)) ||
                        !append_byte(out, capacity, &length, 0x80 | ((codepoint >> 6) & 0x3f)) ||
                        !append_byte(out, capacity, &length, 0x80 | (codepoint & 0x3f))) return false;
                }
                continue;
            }
            if (ch == 'b') ch = '\b';
            else if (ch == 'f') ch = '\f';
            else if (ch == 'n') ch = '\n';
            else if (ch == 'r') ch = '\r';
            else if (ch == 't') ch = '\t';
            else if (ch != '"' && ch != '\\' && ch != '/') return false;
        }
        if (!append_byte(out, capacity, &length, ch)) return false;
    }
    if (*p != '"') return false;
    ++p;
    out[length] = '\0';
    return true;
}

bool json_color(const char*& p, uint32_t* color) {
    char value[8] = {};
    if (!json_string(p, value, sizeof(value)) || value[0] != '#') return false;
    uint32_t result = 0;
    for (unsigned i = 1; i < 7; ++i) {
        if (!value[i]) return false;
        const int digit = hex_digit(value[i]);
        if (digit < 0) return false;
        result = (result << 4) | static_cast<uint32_t>(digit);
    }
    if (value[7]) return false;
    *color = result;
    return true;
}

bool json_radius(const char*& p, int32_t* radius) {
    if (*p == '"') {
        char value[8];
        return json_string(p, value, sizeof(value)) && same_text(value, "auto");
    }
    if (*p < '0' || *p > '9') return false;
    int32_t result = 0;
    const bool leading_zero = *p == '0';
    do {
        result = result * 10 + (*p++ - '0');
        if (result > 32767) return false;
    } while (*p >= '0' && *p <= '9' && !leading_zero);
    *radius = result;
    return true;
}

bool object_key(const char*& p, char* key, size_t size) {
    skip_space(p);
    if (!json_string(p, key, size)) return false;
    skip_space(p);
    if (!consume(p, ":")) return false;
    skip_space(p);
    return true;
}

bool object_next(const char*& p, bool* more) {
    skip_space(p);
    if (consume(p, "}")) { *more = false; return true; }
    if (!consume(p, ",")) return false;
    skip_space(p);
    *more = true;
    return true;
}

bool color_object(const char*& p, Colors* colors) {
    if (!consume(p, "{")) return false;
    skip_space(p);
    if (consume(p, "}")) return true;
    bool more = true;
    while (more) {
        char key[16];
        if (!object_key(p, key, sizeof(key))) return false;
        if (same_text(key, "background")) {
            if (colors->has_background || !json_color(p, &colors->background)) return false;
            colors->has_background = true;
        } else if (same_text(key, "foreground")) {
            if (colors->has_foreground || !json_color(p, &colors->foreground)) return false;
            colors->has_foreground = true;
        } else return false;
        if (!object_next(p, &more)) return false;
    }
    return true;
}

bool category_object(const char*& p, Style* style) {
    if (!consume(p, "{")) return false;
    skip_space(p);
    if (consume(p, "}")) return true;
    unsigned seen = 0;
    bool more = true;
    while (more) {
        char key[16];
        if (!object_key(p, key, sizeof(key))) return false;
        const int index = same_text(key, "numbers") ? 0 : same_text(key, "operators") ? 1 :
                          same_text(key, "functions") ? 2 : same_text(key, "memory") ? 3 : -1;
        if (index < 0 || (seen & (1u << index))) return false;
        seen |= 1u << index;
        if (!color_object(p, &style->categories[index]) || !object_next(p, &more)) return false;
    }
    return true;
}

bool parse_style(const char* config, Style* style) {
    *style = {};
    style->radius = 3;
    style->display_radius = 2;
    style->display_alignment = NATIVE_EXTENSION_ALIGN_RIGHT_MID;
    if (!config || !*config) return true;
    const char* p = config;
    skip_space(p);
    if (!consume(p, "{")) return false;
    skip_space(p);
    if (consume(p, "}")) { skip_space(p); return !*p; }
    unsigned seen = 0;
    bool more = true;
    while (more) {
        char key[24];
        if (!object_key(p, key, sizeof(key))) return false;
        const int field = same_text(key, "display_style") ? 0 : same_text(key, "font") ? 1 :
                          same_text(key, "corner_radius") ? 2 : same_text(key, "keys_background") ? 3 :
                          same_text(key, "keys_foreground") ? 4 : same_text(key, "keys_overrides") ? 5 :
                          same_text(key, "display_background") ? 6 : same_text(key, "display_foreground") ? 7 :
                          same_text(key, "display_border_color") ? 8 : same_text(key, "display_corner_radius") ? 9 :
                          same_text(key, "display_font") ? 10 : same_text(key, "display_alignment") ? 11 : -1;
        if (field < 0 || (seen & (1u << field))) return false;
        seen |= 1u << field;
        switch (field) {
            case 0: {
                char value[8];
                if (!json_string(p, value, sizeof(value))) return false;
                if (same_text(value, "epaper")) style->epaper = true;
                else if (!same_text(value, "normal")) return false;
                break;
            }
            case 1:
                if (!json_string(p, style->font, sizeof(style->font)) || !style->font[0]) return false;
                break;
            case 2:
                if (!json_radius(p, &style->radius)) return false;
                break;
            case 3:
                if (!json_color(p, &style->defaults.background)) return false;
                style->defaults.has_background = true;
                break;
            case 4:
                if (!json_color(p, &style->defaults.foreground)) return false;
                style->defaults.has_foreground = true;
                break;
            case 5:
                if (!category_object(p, style)) return false;
                break;
            case 6:
                if (!json_color(p, &style->display.background)) return false;
                style->display.has_background = true;
                break;
            case 7:
                if (!json_color(p, &style->display.foreground)) return false;
                style->display.has_foreground = true;
                break;
            case 8:
                if (!json_color(p, &style->display_border_color)) return false;
                style->has_display_border_color = true;
                break;
            case 9:
                if (!json_radius(p, &style->display_radius)) return false;
                break;
            case 10:
                if (!json_string(p, style->display_font, sizeof(style->display_font)) ||
                    !style->display_font[0]) return false;
                break;
            case 11: {
                char value[8];
                if (!json_string(p, value, sizeof(value))) return false;
                if (same_text(value, "left")) style->display_alignment = NATIVE_EXTENSION_ALIGN_LEFT_MID;
                else if (same_text(value, "center")) style->display_alignment = NATIVE_EXTENSION_ALIGN_CENTER;
                else if (same_text(value, "right")) style->display_alignment = NATIVE_EXTENSION_ALIGN_RIGHT_MID;
                else return false;
                break;
            }
        }
        if (!object_next(p, &more)) return false;
    }
    skip_space(p);
    return !*p;
}

const char* key_text(calculator::Key key) {
    using namespace calculator;
    switch (key) {
        case MemoryClear: return "MC";
        case MemoryRecall: return "MR";
        case MemoryAdd: return "M+";
        case MemorySubtract: return "M-";
        case SquareRoot: return "sqrt";
        case Digit7: return "7";
        case Digit8: return "8";
        case Digit9: return "9";
        case Divide: return "/";
        case Percent: return "%";
        case Digit4: return "4";
        case Digit5: return "5";
        case Digit6: return "6";
        case Multiply: return "*";
        case Clear: return "C";
        case Digit1: return "1";
        case Digit2: return "2";
        case Digit3: return "3";
        case Subtract: return "-";
        case Sign: return "+/-";
        case Digit0: return "0";
        case Decimal: return ".";
        case Equals: return "=";
        case Add: return "+";
    }
    return "";
}

uint8_t fit_font(const NativeExtensionHostApi* host, const char* font, const char* text,
                 int32_t width, int32_t height) {
    const uint8_t sizes[] = {36, 32, 24, 18, 14, 12};
    for (uint8_t size : sizes) {
        if (size <= height - 4 &&
            host->canvas->canvas_measure_text(text, font, size) <= width - 8) return size;
    }
    return 12;
}

bool update_display(Instance* instance) {
    char text[32];
    calculator::display(instance->calculator, text);
    if (same_text(text, instance->displayed)) return false;
    unsigned index = 0;
    do { instance->displayed[index] = text[index]; } while (text[index++]);
    const auto* ui = instance->host->ui;
    ui->label_set_font(instance->display, instance->display_font,
                       fit_font(instance->host, instance->display_font, text,
                                instance->display_width, instance->display_height));
    ui->label_set_text(instance->display, text);
    const int32_t inset = instance->display_alignment == NATIVE_EXTENSION_ALIGN_LEFT_MID ? 4 :
                          instance->display_alignment == NATIVE_EXTENSION_ALIGN_RIGHT_MID ? -4 : 0;
    ui->obj_align(instance->display, instance->display_alignment, inset, 0);
    return true;
}

void key_pressed(void* context, void*, NativeExtensionLvglEventCode event_code) {
    if (event_code != NATIVE_EXTENSION_EVENT_CLICKED) return;
    auto* key = static_cast<KeyBinding*>(context);
    calculator::press(&key->instance->calculator, key->key, key->instance->host->core->math_sqrt);
    if (update_display(key->instance)) key->instance->host->ui->interaction_changed();
}

unsigned key_category(calculator::Key key) {
    using namespace calculator;
    switch (key) {
        case MemoryClear: case MemoryRecall: case MemoryAdd: case MemorySubtract: return 3;
        case Add: case Subtract: case Multiply: case Divide: case Equals: return 1;
        case Clear: case Sign: case Percent: case SquareRoot: return 2;
        default: return 0;
    }
}

bool make_ui(Instance* instance, void* root, const Style& style) {
    const auto* ui = instance->host->ui;
    const int32_t width = ui->obj_get_width(root);
    const int32_t height = ui->obj_get_height(root);
    instance->panel = ui->button_create(root);
    if (!instance->panel) return false;
    ui->obj_set_size(instance->panel, width, height);
    ui->obj_set_pos(instance->panel, 0, 0);
    ui->obj_set_padding(instance->panel, 0);
    ui->obj_set_border(instance->panel, 0, 0, 0);
    ui->obj_set_bg_color(instance->panel, 0, 0);
    ui->obj_set_clickable(instance->panel, false);

    NativeExtensionButtonSnapshot colors{};
    if (!instance->host->button->get(instance->extension_context, instance->id, &colors)) return false;
    calculator::Layout layout{};
    if (!calculator::layout(width, height, &layout)) {
        void* message = ui->label_create(instance->panel);
        if (!message) return false;
        ui->label_set_text(message, "Make widget bigger");
        ui->obj_set_text_color(message, colors.foreground_rgb);
        ui->label_set_font(message, instance->font, 12);
        ui->label_set_wrap(message, width);
        ui->obj_center(message);
        return true;
    }

    void* display = ui->button_create(instance->panel);
    if (!display) return false;
    instance->display_width = width - 2 * layout.gap;
    instance->display_height = layout.display_height;
    ui->obj_set_pos(display, layout.gap, layout.gap);
    ui->obj_set_size(display, instance->display_width, layout.display_height);
    const uint32_t display_background = style.display.has_background ?
        style.display.background : colors.background_rgb;
    const uint32_t display_foreground = style.display.has_foreground ?
        style.display.foreground : colors.foreground_rgb;
    const uint32_t display_border = style.has_display_border_color ?
        style.display_border_color : display_foreground;
    ui->obj_set_bg_color(display, display_background, 255);
    ui->obj_set_border(display, display_border, 1, style.display_radius);
    ui->obj_set_text_color(display, display_foreground);
    ui->obj_set_clickable(display, false);
    instance->display = ui->label_create(display);
    if (!instance->display) return false;
    update_display(instance);

    const int32_t grid_width = 5 * layout.key_width + 4 * layout.gap;
    const int32_t grid_x = (width - grid_width) / 2;
    const int32_t grid_y = 2 * layout.gap + layout.display_height;
    for (unsigned index = 0; index < 24; ++index) {
        const auto key = static_cast<calculator::Key>(index);
        const int row = static_cast<int>(index / 5);
        int col = static_cast<int>(index % 5);
        if (key == calculator::Add) col = 4;
        const int span = key == calculator::Equals ? 2 : 1;
        const Colors& category = style.categories[key_category(key)];
        const uint32_t background = category.has_background ? category.background :
            style.defaults.has_background ? style.defaults.background : colors.background_rgb;
        const uint32_t foreground = category.has_foreground ? category.foreground :
            style.defaults.has_foreground ? style.defaults.foreground : colors.foreground_rgb;
        void* button = ui->button_create(instance->panel);
        if (!button) return false;
        ui->obj_set_pos(button, grid_x + col * (layout.key_width + layout.gap),
                        grid_y + row * (layout.key_height + layout.gap));
        ui->obj_set_size(button, span * layout.key_width + (span - 1) * layout.gap,
                         layout.key_height);
        ui->obj_set_bg_color(button, background, 255);
        ui->obj_set_border(button, foreground, 1, style.radius);
        ui->obj_set_text_color(button, foreground);
        ui->button_set_press_feedback(button, !style.epaper && !ui->display_disables_animations());
        void* label = ui->label_create(button);
        if (!label) return false;
        const char* text = key_text(key);
        ui->label_set_font(label, instance->font,
                           fit_font(instance->host, instance->font, text,
                                    span * layout.key_width + (span - 1) * layout.gap,
                                    layout.key_height));
        ui->label_set_text(label, text);
        ui->obj_center(label);
        KeyBinding& binding = instance->keys[index];
        binding.instance = instance;
        binding.key = key;
        binding.event = {key_pressed, &binding};
        if (!ui->event_add(button, NATIVE_EXTENSION_EVENT_CLICKED, &binding.event)) return false;
    }
    return true;
}

} // namespace

extern "C" bool native_extension_create_instance(const NativeExtensionHostApi* host,
                                                   void* extension_context, uint32_t instance_id,
                                                   void* root, const char* config_json) {
    if (!host || !host->core || !host->ui || !host->canvas || !host->button || !root ||
        !NATIVE_EXTENSION_API_HAS(host->ui, NativeExtensionUiApi, label_set_font) ||
        !NATIVE_EXTENSION_API_HAS(host->canvas, NativeExtensionCanvasApi, canvas_measure_text) ||
        !NATIVE_EXTENSION_API_HAS(host->button, NativeExtensionButtonApi, get)) return false;
    Style style{};
    if (!parse_style(config_json, &style)) {
        host->core->log(NATIVE_EXTENSION_LOG_ERROR, "Calculator: invalid configuration");
        return false;
    }
    auto* instance = static_cast<Instance*>(host->core->alloc(sizeof(Instance)));
    if (!instance) return false;
    *instance = {};
    instance->host = host;
    instance->extension_context = extension_context;
    instance->id = instance_id;
    for (size_t i = 0; i < sizeof(instance->font); ++i) instance->font[i] = style.font[i];
    const char* display_font = style.display_font[0] ? style.display_font : style.font;
    for (size_t i = 0; i < sizeof(instance->display_font); ++i) instance->display_font[i] = display_font[i];
    instance->display_alignment = style.display_alignment;
    calculator::reset(&instance->calculator);
    if (!make_ui(instance, root, style)) {
        if (instance->panel) host->ui->obj_delete(instance->panel);
        host->core->free(instance);
        return false;
    }
    instance->next = static_cast<Instance*>(host->core->context_get_data(extension_context));
    host->core->context_set_data(extension_context, instance);
    return true;
}

extern "C" void native_extension_destroy_instance(const NativeExtensionHostApi* host,
                                                    void* extension_context, uint32_t instance_id) {
    auto* instance = static_cast<Instance*>(host->core->context_get_data(extension_context));
    Instance* previous = nullptr;
    while (instance && instance->id != instance_id) { previous = instance; instance = instance->next; }
    if (!instance) return;
    if (previous) previous->next = instance->next;
    else host->core->context_set_data(extension_context, instance->next);
    host->ui->obj_delete(instance->panel);
    host->core->free(instance);
}

extern "C" void native_extension_shutdown(const NativeExtensionHostApi* host, void* extension_context) {
    if (host->core->context_get_data(extension_context))
        host->core->log(NATIVE_EXTENSION_LOG_ERROR, "Calculator: instances remain at shutdown");
}
