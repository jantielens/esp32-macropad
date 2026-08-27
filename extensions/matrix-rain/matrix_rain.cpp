#include "native_extension_api.h"

extern "C" const NativeExtensionDescriptor native_extension_descriptor = {
    NATIVE_EXTENSION_DESCRIPTOR_MAGIC, NATIVE_EXTENSION_ABI_VERSION,
    NATIVE_EXTENSION_TARGET_ABI, "matrix-rain", "1.0.0", "Matrix Rain", 100, 0,
};

namespace {

constexpr uint8_t MAX_INSTANCES = 4;
constexpr uint8_t MAX_COLUMNS = 48;
constexpr uint8_t MAX_ROWS = 64;
constexpr uint8_t DEFAULT_FONT_SIZE = 18;
constexpr float DEFAULT_SPEED = 1.0f;
constexpr float MAX_SPEED = 12.0f;
constexpr float DEFAULT_DENSITY = 0.8f;
constexpr float DEFAULT_TRAIL_LENGTH = 1.0f;
constexpr float MIN_TRAIL_LENGTH = 0.25f;
constexpr float MAX_TRAIL_LENGTH = 2.0f;
constexpr uint16_t MAX_GLYPHS_PER_FRAME = 400;
constexpr uint32_t DEFAULT_BACKGROUND_RGB = 0x010603;
constexpr uint32_t CLOCK_RESOLVE_MS = 500;
constexpr uint32_t DEFAULT_SHIFT_MINUTES = 60;
constexpr uint32_t MAX_SHIFT_MINUTES = 30000;
constexpr uint8_t CLOCK_CHARACTERS = 5;
constexpr uint8_t CLOCK_TEXT_CAPACITY = CLOCK_CHARACTERS + 1;
constexpr uint8_t CLOCK_TEMPLATE_CAPACITY = 96;
constexpr uint32_t DEFAULT_RAIN_RGB = 0x00FF41;
constexpr uint32_t DEFAULT_CLOCK_RGB = 0x00FF41;
constexpr int32_t GLYPH_CLEAR_PAD_X = 2;
constexpr int32_t GLYPH_CLEAR_PAD_Y = 4;
constexpr char CHARACTER_SET[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-*/=<>[]{}:;|\\";

struct Column {
    float head;
    float speed;
    uint8_t length;
    uint32_t random;
    bool enabled;
};

struct InstanceState {
    bool active;
    uint32_t instance_id;
    void* extension_context;
    void* canvas;
    void* buffer;
    uint16_t width;
    uint16_t height;
    uint8_t font_size;
    char font_name[16];
    float speed;
    float density;
    float trail_length;
    uint8_t columns;
    uint8_t rows;
    uint16_t cell_width;
    uint16_t cell_height;
    Column rain[MAX_COLUMNS];
    uint32_t random;
    uint32_t last_motion_ms;
    uint32_t active_cells[MAX_COLUMNS][2];
    char clock_template[CLOCK_TEMPLATE_CAPACITY];
    char clock[CLOCK_TEXT_CAPACITY];
    uint32_t next_clock_resolve_ms;
    uint32_t last_shift_ms;
    uint32_t shift_interval_ms;
    uint8_t clock_enabled;
    uint8_t clock_column;
    uint8_t clock_row;
    int8_t clock_shift_y;
    int8_t previous_clock_shift_y;
    uint32_t background_rgb;
    uint32_t rain_rgb;
    uint32_t clock_rgb;
    bool background_initialized;
    bool dirty;
};

struct PackageState { InstanceState instances[MAX_INSTANCES]; };

void copy_text(char* out, size_t capacity, const char* value) {
    if (!out || capacity == 0) return;
    size_t index = 0;
    while (value && value[index] && index + 1 < capacity) { out[index] = value[index]; ++index; }
    out[index] = '\0';
}

bool find_number(const char* json, const char* key, float* out) {
    if (!json || !key || !out) return false;
    for (const char* cursor = json; *cursor; ++cursor) {
        if (*cursor != '"') continue;
        const char* name = cursor + 1;
        const char* key_cursor = key;
        while (*name && *key_cursor && *name == *key_cursor) { ++name; ++key_cursor; }
        if (*key_cursor || *name != '"') continue;
        while (*name && *name != ':') ++name;
        if (*name != ':') return false;
        ++name;
        while (*name == ' ' || *name == '\t') ++name;
        bool negative = false;
        if (*name == '-') { negative = true; ++name; }
        float value = 0.0f;
        bool found = false;
        while (*name >= '0' && *name <= '9') {
            value = value * 10.0f + static_cast<float>(*name++ - '0');
            found = true;
        }
        if (*name == '.') {
            float place = 0.1f;
            ++name;
            while (*name >= '0' && *name <= '9') {
                value += static_cast<float>(*name++ - '0') * place;
                place *= 0.1f;
                found = true;
            }
        }
        if (!found) return false;
        if (*name && *name != ' ' && *name != '\t' && *name != '\r' && *name != '\n' &&
            *name != ',' && *name != '}') return false;
        *out = negative ? -value : value;
        return true;
    }
    return false;
}

bool find_string(const char* json, const char* key, char* out, size_t capacity) {
    if (!json || !key || !out || capacity == 0) return false;
    for (const char* cursor = json; *cursor; ++cursor) {
        if (*cursor != '"') continue;
        const char* name = cursor + 1;
        const char* key_cursor = key;
        while (*name && *key_cursor && *name == *key_cursor) { ++name; ++key_cursor; }
        if (*key_cursor || *name != '"') continue;
        while (*name && *name != ':') ++name;
        if (*name != ':') return false;
        ++name;
        while (*name == ' ' || *name == '\t') ++name;
        if (*name++ != '"') return false;
        size_t length = 0;
        while (*name && *name != '"' && length + 1 < capacity) out[length++] = *name++;
        out[length] = '\0';
        return true;
    }
    return false;
}

bool parse_hex_color(const char* value, uint32_t* color) {
    if (!value || !color) return false;
    if (*value == '#') ++value;
    uint32_t parsed = 0;
    for (uint8_t digit = 0; digit < 6; ++digit) {
        const char character = value[digit];
        uint8_t nibble = 0;
        if (character >= '0' && character <= '9') nibble = static_cast<uint8_t>(character - '0');
        else if (character >= 'A' && character <= 'F') nibble = static_cast<uint8_t>(character - 'A' + 10);
        else if (character >= 'a' && character <= 'f') nibble = static_cast<uint8_t>(character - 'a' + 10);
        else return false;
        parsed = (parsed << 4) | nibble;
    }
    if (value[6] != '\0') return false;
    *color = parsed;
    return true;
}

bool parse_color_config(const char* json, const char* key, uint32_t* color) {
    char value[16] = {};
    return find_string(json, key, value, sizeof(value)) && parse_hex_color(value, color);
}

uint32_t next_random(InstanceState* instance) {
    uint32_t value = instance->random;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    instance->random = value ? value : 0xA341316Cu;
    return instance->random;
}

PackageState* package_state(const NativeExtensionHostApi* host, void* context) {
    PackageState* state = static_cast<PackageState*>(host->core->context_get_data(context));
    if (state) return state;
    state = static_cast<PackageState*>(host->core->alloc(sizeof(PackageState)));
    if (!state) return nullptr;
    for (auto& instance : state->instances) instance = {};
    host->core->context_set_data(context, state);
    return state;
}

InstanceState* find_instance(PackageState* state, uint32_t instance_id) {
    for (auto& instance : state->instances)
        if (instance.active && instance.instance_id == instance_id) return &instance;
    return nullptr;
}

InstanceState* create_instance(PackageState* state, uint32_t instance_id) {
    for (auto& instance : state->instances) if (!instance.active) {
        instance = {};
        instance.active = true;
        instance.instance_id = instance_id;
        instance.random = instance_id ^ 0x9E3779B9u;
        return &instance;
    }
    return nullptr;
}

uint8_t clamp_font_size(float value) {
    if (value < 12) return DEFAULT_FONT_SIZE;
    if (value > 48) return 48;
    return static_cast<uint8_t>(value);
}

uint32_t fade_color(uint32_t background, uint32_t color, uint8_t intensity) {
    const int32_t background_red = (background >> 16) & 0xFFu;
    const int32_t background_green = (background >> 8) & 0xFFu;
    const int32_t background_blue = background & 0xFFu;
    const int32_t color_red = (color >> 16) & 0xFFu;
    const int32_t color_green = (color >> 8) & 0xFFu;
    const int32_t color_blue = color & 0xFFu;
    const uint32_t red = background_red + (color_red - background_red) * intensity / 255u;
    const uint32_t green = background_green + (color_green - background_green) * intensity / 255u;
    const uint32_t blue = background_blue + (color_blue - background_blue) * intensity / 255u;
    return (red << 16) | (green << 8) | blue;
}

bool text_equals(const char* left, const char* right) {
    size_t index = 0;
    while (left[index] && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

bool normalize_clock(const char* source, char* out, size_t capacity) {
    if (!source || !out || capacity < CLOCK_TEXT_CAPACITY) return false;
    char digits[4];
    uint8_t count = 0;
    for (; *source && count < sizeof(digits); ++source)
        if (*source >= '0' && *source <= '9') digits[count++] = *source;
    if (count != sizeof(digits)) return false;
    out[0] = digits[0];
    out[1] = digits[1];
    out[2] = ':';
    out[3] = digits[2];
    out[4] = digits[3];
    out[5] = '\0';
    return true;
}

uint32_t parse_shift_interval(const char* json) {
    float minutes = 0.0f;
    if (!find_number(json, "burn_in_shift_minutes", &minutes)) return DEFAULT_SHIFT_MINUTES * 60000u;
    if (minutes <= 0.0f) return 0;
    if (minutes > static_cast<float>(MAX_SHIFT_MINUTES)) minutes = static_cast<float>(MAX_SHIFT_MINUTES);
    return static_cast<uint32_t>(minutes * 60000.0f);
}

uint8_t clock_render_row(const InstanceState* instance) {
    int16_t row = static_cast<int16_t>(instance->clock_row) + instance->clock_shift_y;
    if (row < 0) row = 0;
    if (row >= instance->rows) row = static_cast<int16_t>(instance->rows - 1);
    return static_cast<uint8_t>(row);
}

void reset_column(InstanceState* instance, Column* column, uint8_t index) {
    column->random = next_random(instance);
    const uint8_t base_length = static_cast<uint8_t>(5 + column->random % (instance->rows > 10 ? instance->rows / 2 : 5));
    const uint8_t scaled_length = static_cast<uint8_t>(base_length * instance->trail_length);
    column->length = scaled_length == 0 ? 1 : (scaled_length > instance->rows ? instance->rows : scaled_length);
    column->head = -static_cast<float>(column->random % (instance->rows + column->length));
    column->speed = column->enabled
        ? (0.036f + static_cast<float>(column->random % 12) * 0.006f) * instance->speed
        : 0.0f;
    (void)index;
}

void resolve_clock(const NativeExtensionHostApi* host, InstanceState* instance) {
    char resolved[CLOCK_TEMPLATE_CAPACITY] = {};
    char clock[CLOCK_TEXT_CAPACITY] = {};
    if (!host->binding->resolve(instance->extension_context, instance->instance_id,
                                instance->clock_template, resolved, sizeof(resolved)) ||
        !normalize_clock(resolved, clock, sizeof(clock))) return;
    if (!text_equals(instance->clock, clock)) copy_text(instance->clock, sizeof(instance->clock), clock);
}

void draw_clock(const NativeExtensionHostApi* host, const InstanceState* instance) {
    if (!instance->clock_enabled) return;
    char character[2] = {0, '\0'};
    const uint8_t row = clock_render_row(instance);
    for (uint8_t index = 0; index < CLOCK_CHARACTERS; ++index) {
        const int32_t x = (instance->clock_column + index) * instance->cell_width;
        const int32_t y = row * instance->cell_height;
        character[0] = instance->clock[index];
        host->canvas->canvas_draw_text(instance->canvas, x, y, character,
                                       instance->font_name, instance->font_size, instance->clock_rgb);
    }
}

void clear_clock(const NativeExtensionHostApi* host, const InstanceState* instance) {
    if (!instance->clock_enabled) return;
    const int8_t shifts[] = {instance->previous_clock_shift_y, instance->clock_shift_y};
    for (uint8_t shift_index = 0; shift_index < 2; ++shift_index) {
        int16_t row = static_cast<int16_t>(instance->clock_row) + shifts[shift_index];
        if (row < 0) row = 0;
        if (row >= instance->rows) row = static_cast<int16_t>(instance->rows - 1);
        for (uint8_t index = 0; index < CLOCK_CHARACTERS; ++index)
            host->canvas->canvas_fill_rect(instance->canvas,
                                           (instance->clock_column + index) * instance->cell_width - GLYPH_CLEAR_PAD_X,
                                           row * instance->cell_height - GLYPH_CLEAR_PAD_Y,
                                           instance->cell_width + GLYPH_CLEAR_PAD_X * 2,
                                           instance->cell_height + GLYPH_CLEAR_PAD_Y * 2, instance->background_rgb);
    }
}

uint16_t render(const NativeExtensionHostApi* host, InstanceState* instance) {
    if (!instance->background_initialized) {
        host->canvas->canvas_fill_rect(instance->canvas, 0, 0, instance->width, instance->height, instance->background_rgb);
        instance->background_initialized = true;
    } else {
        for (uint8_t column = 0; column < instance->columns; ++column) {
            for (uint8_t word = 0; word < 2; ++word) {
                uint32_t cells = instance->active_cells[column][word];
                uint8_t row = static_cast<uint8_t>(word * 32);
                while (cells) {
                    while (!(cells & 1u)) {
                        cells >>= 1;
                        ++row;
                    }
                    const uint8_t first_row = row;
                    while (cells & 1u) {
                        cells >>= 1;
                        ++row;
                    }
                    host->canvas->canvas_fill_rect(instance->canvas,
                                                    column * instance->cell_width - GLYPH_CLEAR_PAD_X,
                                                    first_row * instance->cell_height - GLYPH_CLEAR_PAD_Y,
                                                    instance->cell_width + GLYPH_CLEAR_PAD_X * 2,
                                                    (row - first_row) * instance->cell_height + GLYPH_CLEAR_PAD_Y * 2,
                                                    instance->background_rgb);
                }
                instance->active_cells[column][word] = 0;
            }
        }
    }
    clear_clock(host, instance);
    char glyph[2] = {0, 0};
    uint16_t rendered_glyphs = 0;
    uint8_t visible_columns = 0;
    for (uint8_t column = 0; column < instance->columns; ++column)
        if (instance->rain[column].speed != 0.0f && instance->rain[column].head >= 0.0f &&
            instance->rain[column].head - instance->rain[column].length < instance->rows) ++visible_columns;
    const uint8_t glyphs_per_column = visible_columns
        ? static_cast<uint8_t>(MAX_GLYPHS_PER_FRAME / visible_columns)
        : 0;
    for (uint8_t column = 0; column < instance->columns; ++column) {
        const Column& drop = instance->rain[column];
        if (drop.speed == 0.0f) continue;
        if (drop.head < 0.0f || drop.head - drop.length >= instance->rows) continue;
        uint8_t rendered_for_column = 0;
        for (int16_t row = static_cast<int16_t>(instance->rows) - 1; row >= 0; --row) {
            const float distance = drop.head - static_cast<float>(row);
            if (distance < 0.0f || distance >= drop.length) continue;
            const bool clock_cell = instance->clock_enabled && row == clock_render_row(instance) &&
                column >= instance->clock_column && column < instance->clock_column + CLOCK_CHARACTERS;
            if (clock_cell) continue;
            uint8_t intensity = static_cast<uint8_t>(255.0f * (1.0f - distance / drop.length));
            if (distance < 1.0f) intensity = 255;
            glyph[0] = CHARACTER_SET[(drop.random + row * 17u + column * 31u) % (sizeof(CHARACTER_SET) - 1)];
            host->canvas->canvas_draw_text(instance->canvas, column * instance->cell_width,
                                            row * instance->cell_height, glyph, instance->font_name,
                                            instance->font_size, fade_color(instance->background_rgb, instance->rain_rgb, intensity));
            instance->active_cells[column][row / 32] |= 1u << (row % 32);
            ++rendered_glyphs;
            if (++rendered_for_column >= glyphs_per_column) break;
        }
    }
    draw_clock(host, instance);
    host->canvas->canvas_invalidate_rect(instance->canvas, 0, 0, instance->width, instance->height);
    instance->previous_clock_shift_y = instance->clock_shift_y;
    instance->dirty = false;
    return rendered_glyphs;
}

} // namespace

extern "C" void native_extension_create_instance(const NativeExtensionHostApi* host, void* extension_context,
                                                   uint32_t instance_id, void* root, const char* config_json) {
    if (!host || !host->core || !host->ui || !host->canvas || !root) return;
    PackageState* state = package_state(host, extension_context);
    InstanceState* instance = state ? create_instance(state, instance_id) : nullptr;
    if (!instance) return;
    instance->extension_context = extension_context;
    instance->width = static_cast<uint16_t>(host->ui->obj_get_width(root));
    instance->height = static_cast<uint16_t>(host->ui->obj_get_height(root));
    instance->background_rgb = DEFAULT_BACKGROUND_RGB;
    instance->rain_rgb = DEFAULT_RAIN_RGB;
    instance->clock_rgb = DEFAULT_CLOCK_RGB;
    NativeExtensionButtonSnapshot button = {};
    if (host->button && host->button->get(extension_context, instance_id, &button))
        instance->background_rgb = button.background_rgb;
    parse_color_config(config_json, "rain_color", &instance->rain_rgb);
    parse_color_config(config_json, "clock_color", &instance->clock_rgb);
    instance->font_size = DEFAULT_FONT_SIZE;
    float value = 0.0f;
    if (find_number(config_json, "font_size", &value)) instance->font_size = clamp_font_size(value);
    copy_text(instance->font_name, sizeof(instance->font_name), "default");
    find_string(config_json, "font_family", instance->font_name, sizeof(instance->font_name));
    instance->speed = DEFAULT_SPEED;
    if (find_number(config_json, "speed", &value) && value > 0.1f) instance->speed = value > MAX_SPEED ? MAX_SPEED : value;
    instance->density = DEFAULT_DENSITY;
    if (find_number(config_json, "density", &value)) instance->density = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    instance->trail_length = DEFAULT_TRAIL_LENGTH;
    if (find_number(config_json, "trail_length", &value)) {
        instance->trail_length = value < MIN_TRAIL_LENGTH ? MIN_TRAIL_LENGTH
            : (value > MAX_TRAIL_LENGTH ? MAX_TRAIL_LENGTH : value);
    }
    instance->cell_height = instance->font_size;
    instance->cell_width = instance->font_size;
    instance->columns = static_cast<uint8_t>(instance->width / instance->cell_width);
    instance->rows = static_cast<uint8_t>(instance->height / instance->cell_height);
    if (instance->columns > MAX_COLUMNS) instance->columns = MAX_COLUMNS;
    if (instance->rows > MAX_ROWS) instance->rows = MAX_ROWS;
    if (!instance->columns || !instance->rows) { instance->active = false; return; }
    if (find_string(config_json, "clock", instance->clock_template, sizeof(instance->clock_template))) {
        instance->clock_enabled = host->binding != nullptr && instance->columns >= CLOCK_CHARACTERS;
        if (instance->clock_enabled) {
            instance->clock_column = instance->columns > CLOCK_CHARACTERS
                ? static_cast<uint8_t>((instance->columns - CLOCK_CHARACTERS) / 2u) : 0;
            instance->clock_row = static_cast<uint8_t>((instance->rows - 1u) / 2u);
            char resolved[CLOCK_TEMPLATE_CAPACITY] = {};
            if (host->binding->resolve(extension_context, instance_id, instance->clock_template,
                                       resolved, sizeof(resolved)))
                normalize_clock(resolved, instance->clock, sizeof(instance->clock));
            if (!instance->clock[0]) copy_text(instance->clock, sizeof(instance->clock), "00:00");
        }
    }
    instance->canvas = host->canvas->canvas_create(root);
    instance->buffer = host->core->alloc(host->canvas->canvas_buffer_size(instance->width, instance->height));
    if (!instance->canvas || !instance->buffer) { instance->active = false; return; }
    host->canvas->canvas_set_buffer(instance->canvas, instance->buffer, instance->width, instance->height);
    for (uint8_t column = 0; column < instance->columns; ++column) {
        Column& drop = instance->rain[column];
        drop.enabled = static_cast<float>(next_random(instance) % 1000) / 1000.0f < instance->density;
        reset_column(instance, &drop, column);
    }
    instance->dirty = true;
    instance->last_motion_ms = host->core->millis();
    instance->next_clock_resolve_ms = instance->last_motion_ms + CLOCK_RESOLVE_MS;
    instance->last_shift_ms = instance->last_motion_ms;
    instance->shift_interval_ms = parse_shift_interval(config_json);
}

extern "C" void native_extension_destroy_instance(const NativeExtensionHostApi* host, void* extension_context,
                                                   uint32_t instance_id) {
    PackageState* state = host ? static_cast<PackageState*>(host->core->context_get_data(extension_context)) : nullptr;
    InstanceState* instance = state ? find_instance(state, instance_id) : nullptr;
    if (!instance) return;
    if (instance->buffer) host->core->free(instance->buffer);
    *instance = {};
}

extern "C" void native_extension_shutdown(const NativeExtensionHostApi* host, void* extension_context) {
    if (!host || !host->core) return;
    PackageState* state = static_cast<PackageState*>(host->core->context_get_data(extension_context));
    if (state) host->core->free(state);
    host->core->context_set_data(extension_context, nullptr);
}

extern "C" void native_extension_tick(const NativeExtensionHostApi* host, void* extension_context, uint32_t instance_id) {
    if (!host || !host->core || !host->canvas) return;
    PackageState* state = static_cast<PackageState*>(host->core->context_get_data(extension_context));
    InstanceState* instance = state ? find_instance(state, instance_id) : nullptr;
    if (!instance) return;
    const uint32_t now = host->core->millis();
    uint32_t elapsed_ms = now - instance->last_motion_ms;
    if (elapsed_ms > 250u) elapsed_ms = 250u;
    instance->last_motion_ms = now;
    if (instance->clock_enabled && static_cast<int32_t>(now - instance->next_clock_resolve_ms) >= 0) {
        resolve_clock(host, instance);
        instance->next_clock_resolve_ms = now + CLOCK_RESOLVE_MS;
    }
    if (instance->clock_enabled && instance->shift_interval_ms &&
        now - instance->last_shift_ms >= instance->shift_interval_ms) {
        instance->previous_clock_shift_y = instance->clock_shift_y;
        instance->clock_shift_y = instance->clock_shift_y == 1 ? -1 : instance->clock_shift_y + 1;
        instance->last_shift_ms = now;
    }
    for (uint8_t column = 0; column < instance->columns; ++column) {
        Column& drop = instance->rain[column];
        if (drop.speed == 0.0f) {
            continue;
        }
        drop.head += drop.speed * static_cast<float>(elapsed_ms) / 100.0f;
        if (drop.head - drop.length > instance->rows) reset_column(instance, &drop, column);
    }
    render(host, instance);
}

extern "C" NativeExtensionEventResult native_extension_on_tap(const NativeExtensionHostApi*, void*, uint32_t) {
    return NATIVE_EXTENSION_PASS_THROUGH;
}

extern "C" NativeExtensionEventResult native_extension_on_long_press(const NativeExtensionHostApi*, void*, uint32_t) {
    return NATIVE_EXTENSION_PASS_THROUGH;
}