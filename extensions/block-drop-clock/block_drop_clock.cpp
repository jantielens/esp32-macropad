#include "native_extension_api.h"

extern "C" const NativeExtensionDescriptor native_extension_descriptor = {
    NATIVE_EXTENSION_DESCRIPTOR_MAGIC, NATIVE_EXTENSION_ABI_VERSION,
    NATIVE_EXTENSION_TARGET_ABI, "block-drop-clock", "1.0.0", "Block Drop Clock",
    50, 0,
};

namespace {

constexpr uint8_t MAX_INSTANCES = 4;
constexpr uint8_t DIGIT_ROWS = 5;
constexpr uint8_t DIGIT_COLUMNS = 3;
constexpr uint8_t MAX_CLOCK_COLUMNS = 17;
constexpr uint8_t CLOCK_TEXT_MAX_LEN = 8;
constexpr uint8_t GAME_ROWS = 7;
constexpr uint8_t MIN_GAME_COLUMNS = 6;
constexpr uint8_t MAX_GAME_COLUMNS = MAX_CLOCK_COLUMNS;
constexpr uint8_t DEFAULT_COLOR_VARIANTS = 7;
constexpr uint8_t CUSTOM_BLOCK_COLOR_INDEX = DEFAULT_COLOR_VARIANTS;
constexpr uint8_t CUSTOM_CLOCK_COLOR_INDEX = DEFAULT_COLOR_VARIANTS * 2;
constexpr uint8_t SPRITE_COLOR_VARIANTS = DEFAULT_COLOR_VARIANTS * 3;
constexpr uint32_t DEFAULT_SHIFT_MINUTES = 60;
constexpr uint32_t CELL_FALL_MS = 180;
constexpr uint32_t GAME_FALL_MS = 420;
constexpr uint32_t CLOCK_RESOLVE_MS = 500;
constexpr int8_t ROTATE_DECISION_ROW = 3;
constexpr int8_t MOVE_DECISION_ROW = 5;
constexpr uint8_t MAX_SAFE_GAME_HEIGHT = 5;

constexpr uint32_t BLOCK_COLORS[] = {
    0x55D7E8, 0xF3D24B, 0xB864D5, 0x4B7DE8, 0xE88B3C, 0x65C95B, 0xE85454,
};

constexpr uint8_t SPRITE_SIZE = 8;
constexpr uint32_t BLOCK_SPRITE[SPRITE_SIZE * SPRITE_SIZE] = {
    0xFFBCBC, 0xFFBBBB, 0xFFBBBB, 0xFFBBBB, 0xFFBBBB, 0xFFBCBC, 0xFFBCBC, 0xFF8888,
    0xFFBBBB, 0xDE0000, 0xFF4444, 0xFF4444, 0xFF4545, 0xFF8989, 0xFF8888, 0x780033,
    0xFFBBBB, 0xFF4444, 0xFF4444, 0xFF4545, 0xFF8989, 0xFF8989, 0xFF8888, 0x780033,
    0xFFBBBB, 0xFF4444, 0xFF4545, 0xFF8989, 0xFF8989, 0xFF8888, 0xFF4444, 0x780033,
    0xFFBBBB, 0xFF4545, 0xFF8989, 0xFF8989, 0xFF8888, 0xFF4444, 0xFF4444, 0x780033,
    0xFFBCBC, 0xFF8989, 0xFF8989, 0xFF8888, 0xFF4444, 0xFF4444, 0xDE0000, 0x780033,
    0xFFBCBC, 0xFF8888, 0xFF8888, 0xFF4444, 0xFF4444, 0xDE0000, 0xDD0000, 0x780033,
    0xFFBBBB, 0x780033, 0x780033, 0x780033, 0x780033, 0x780033, 0x780033, 0x780134,
};

constexpr uint8_t DIGITS[10][DIGIT_ROWS] = {
    {0b111, 0b101, 0b101, 0b101, 0b111}, {0b010, 0b110, 0b010, 0b010, 0b111},
    {0b111, 0b001, 0b111, 0b100, 0b111}, {0b111, 0b001, 0b111, 0b001, 0b111},
    {0b101, 0b101, 0b111, 0b001, 0b001}, {0b111, 0b100, 0b111, 0b001, 0b111},
    {0b111, 0b100, 0b111, 0b101, 0b111}, {0b111, 0b001, 0b010, 0b010, 0b010},
    {0b111, 0b101, 0b111, 0b101, 0b111}, {0b111, 0b101, 0b111, 0b001, 0b111},
};

constexpr uint8_t SHAPES[7][4][2] = {
    {{0, 0}, {1, 0}, {2, 0}, {3, 0}}, {{0, 0}, {1, 0}, {0, 1}, {1, 1}},
    {{0, 0}, {1, 0}, {2, 0}, {1, 1}}, {{1, 0}, {2, 0}, {0, 1}, {1, 1}},
    {{0, 0}, {1, 0}, {1, 1}, {2, 1}}, {{0, 0}, {0, 1}, {1, 1}, {2, 1}},
    {{2, 0}, {0, 1}, {1, 1}, {2, 1}},
};

struct GamePiece {
    uint8_t active;
    uint8_t shape;
    uint8_t color;
    uint8_t rotation;
    uint8_t target_rotation;
    int8_t x;
    int8_t target_x;
    int8_t y;
};

struct InstanceState {
    uint8_t active;
    uint32_t instance_id;
    void* extension_context;
    void* canvas;
    void* buffer;
    uint16_t width;
    uint16_t height;
    uint16_t cell;
    uint8_t grid_rows;
    uint8_t game_top;
    uint8_t game_columns;
    uint8_t game[GAME_ROWS][MAX_GAME_COLUMNS];
    uint16_t tinted_sprite_colors[SPRITE_COLOR_VARIANTS][SPRITE_SIZE * SPRITE_SIZE];
    GamePiece game_piece;
    GamePiece rendered_piece;
    char time_template[96];
    char clock[CLOCK_TEXT_MAX_LEN];
    uint32_t next_clock_resolve_ms;
    uint32_t next_game_step_ms;
    uint32_t last_shift_ms;
    uint32_t shift_interval_ms;
    uint16_t speed_percent;
    uint32_t block_color;
    uint32_t clock_color;
    uint8_t block_color_set;
    uint8_t clock_color_set;
    int8_t shift_x;
    int8_t shift_y;
    uint8_t shape_index;
    uint32_t decision_state;
    uint8_t render_dirty;
    uint8_t game_dirty;
    uint8_t clock_dirty;
    uint8_t full_render;
    uint8_t board_dirty;
    uint8_t rendered_piece_valid;
    uint32_t perf_window_started_ms;
    uint16_t perf_tick_count;
    uint16_t perf_render_count;
    uint32_t perf_tick_total_ms;
    uint32_t perf_render_total_ms;
    uint32_t perf_clear_total_ms;
    uint32_t perf_draw_total_ms;
    uint32_t perf_fill_calls;
    uint16_t perf_game_dirty_count;
    uint16_t perf_clock_dirty_count;
    uint16_t perf_shift_dirty_count;
    uint16_t perf_game_interval_ms;
};

struct PackageState { InstanceState instances[MAX_INSTANCES]; };

void copy_text(char* out, size_t capacity, const char* value) {
    if (!out || capacity == 0) return;
    size_t index = 0;
    while (value && value[index] && index + 1 < capacity) { out[index] = value[index]; ++index; }
    out[index] = '\0';
}

bool text_equals(const char* left, const char* right) {
    size_t index = 0;
    while (left[index] && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

void mark_game_dirty(InstanceState* instance) {
    instance->render_dirty = true;
    instance->game_dirty = true;
    ++instance->perf_game_dirty_count;
}

void mark_clock_dirty(InstanceState* instance) {
    instance->render_dirty = true;
    instance->clock_dirty = true;
}

char* append_uint(char* out, uint32_t value) {
    char digits[10];
    uint8_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10u);
        value /= 10u;
    } while (value);
    while (count) *out++ = digits[--count];
    return out;
}

char* append_text(char* out, const char* text) {
    while (*text) *out++ = *text++;
    return out;
}

void log_performance(const NativeExtensionHostApi* host, InstanceState* instance, uint32_t now) {
    if (!instance->perf_window_started_ms) instance->perf_window_started_ms = now;
    if (now - instance->perf_window_started_ms < 1000u) return;
    char message[208];
    char* out = append_text(message, "block drop perf: tick=");
    out = append_uint(out, instance->perf_tick_count);
    out = append_text(out, " render=");
    out = append_uint(out, instance->perf_render_count);
    out = append_text(out, " tick_ms=");
    out = append_uint(out, instance->perf_tick_count ? instance->perf_tick_total_ms / instance->perf_tick_count : 0);
    out = append_text(out, " render_ms=");
    out = append_uint(out, instance->perf_render_count ? instance->perf_render_total_ms / instance->perf_render_count : 0);
    out = append_text(out, " clear_ms=");
    out = append_uint(out, instance->perf_render_count ? instance->perf_clear_total_ms / instance->perf_render_count : 0);
    out = append_text(out, " draw_ms=");
    out = append_uint(out, instance->perf_render_count ? instance->perf_draw_total_ms / instance->perf_render_count : 0);
    out = append_text(out, " fills=");
    out = append_uint(out, instance->perf_fill_calls);
    out = append_text(out, " game=");
    out = append_uint(out, instance->perf_game_dirty_count);
    out = append_text(out, " clock=");
    out = append_uint(out, instance->perf_clock_dirty_count);
    out = append_text(out, " shift=");
    out = append_uint(out, instance->perf_shift_dirty_count);
    out = append_text(out, " speed=");
    out = append_uint(out, instance->speed_percent);
    out = append_text(out, " step_ms=");
    out = append_uint(out, instance->perf_game_interval_ms);
    *out = '\0';
    host->core->log(NATIVE_EXTENSION_LOG_INFO, message);
    instance->perf_window_started_ms = now;
    instance->perf_tick_count = 0;
    instance->perf_render_count = 0;
    instance->perf_tick_total_ms = 0;
    instance->perf_render_total_ms = 0;
    instance->perf_clear_total_ms = 0;
    instance->perf_draw_total_ms = 0;
    instance->perf_fill_calls = 0;
    instance->perf_game_dirty_count = 0;
    instance->perf_clock_dirty_count = 0;
    instance->perf_shift_dirty_count = 0;
}

float color_lightness(uint32_t rgb) {
    const float red = static_cast<float>((rgb >> 16) & 0xFFu) / 255.0f;
    const float green = static_cast<float>((rgb >> 8) & 0xFFu) / 255.0f;
    const float blue = static_cast<float>(rgb & 0xFFu) / 255.0f;
    const float maximum = red > green ? (red > blue ? red : blue) : (green > blue ? green : blue);
    const float minimum = red < green ? (red < blue ? red : blue) : (green < blue ? green : blue);
    return (maximum + minimum) * 0.5f;
}

uint32_t sprite_tint(uint32_t block_color, uint32_t source_color) {
    const float source_offset = color_lightness(source_color) - color_lightness(0xFF4444u);
    const float target_lightness = color_lightness(block_color) + source_offset;
    const float minimum = target_lightness < 0.0f ? 0.0f : target_lightness;
    const float maximum = minimum > 1.0f ? 1.0f : minimum;
    const float base_lightness = color_lightness(block_color);
    if (base_lightness <= 0.0f) return 0;
    const float scale = maximum / base_lightness;
    const uint32_t red = static_cast<uint32_t>(((block_color >> 16) & 0xFFu) * scale + 0.5f);
    const uint32_t green = static_cast<uint32_t>(((block_color >> 8) & 0xFFu) * scale + 0.5f);
    const uint32_t blue = static_cast<uint32_t>((block_color & 0xFFu) * scale + 0.5f);
    return (red > 255u ? 255u : red) << 16 | (green > 255u ? 255u : green) << 8 | (blue > 255u ? 255u : blue);
}

uint16_t rgb565(uint32_t rgb) {
    return static_cast<uint16_t>(((rgb & 0xF80000u) >> 8) | ((rgb & 0x00FC00u) >> 5) | ((rgb & 0x0000F8u) >> 3));
}

uint8_t shade_channel(uint8_t channel, uint8_t amount) {
    if (amount <= 100) return static_cast<uint8_t>(channel * amount / 100u);
    return static_cast<uint8_t>(channel + (255u - channel) * (amount - 100u) / 100u);
}

uint32_t color_variant(uint32_t color, uint8_t variant) {
    constexpr uint8_t SHADE_LEVELS[DEFAULT_COLOR_VARIANTS] = {72, 86, 100, 114, 128, 92, 106};
    const uint8_t amount = SHADE_LEVELS[variant % DEFAULT_COLOR_VARIANTS];
    const uint8_t red = shade_channel(static_cast<uint8_t>((color >> 16) & 0xFFu), amount);
    const uint8_t green = shade_channel(static_cast<uint8_t>((color >> 8) & 0xFFu), amount);
    const uint8_t blue = shade_channel(static_cast<uint8_t>(color & 0xFFu), amount);
    return (static_cast<uint32_t>(red) << 16) | (static_cast<uint32_t>(green) << 8) | blue;
}

void build_tinted_sprite_colors(InstanceState* instance) {
    for (uint8_t color = 0; color < DEFAULT_COLOR_VARIANTS; ++color)
        for (uint8_t pixel = 0; pixel < SPRITE_SIZE * SPRITE_SIZE; ++pixel)
            instance->tinted_sprite_colors[color][pixel] = rgb565(sprite_tint(BLOCK_COLORS[color], BLOCK_SPRITE[pixel]));
    for (uint8_t variant = 0; variant < DEFAULT_COLOR_VARIANTS; ++variant) {
        if (instance->block_color_set)
            for (uint8_t pixel = 0; pixel < SPRITE_SIZE * SPRITE_SIZE; ++pixel)
                instance->tinted_sprite_colors[CUSTOM_BLOCK_COLOR_INDEX + variant][pixel] =
                    rgb565(sprite_tint(color_variant(instance->block_color, variant), BLOCK_SPRITE[pixel]));
        if (instance->clock_color_set)
            for (uint8_t pixel = 0; pixel < SPRITE_SIZE * SPRITE_SIZE; ++pixel)
                instance->tinted_sprite_colors[CUSTOM_CLOCK_COLOR_INDEX + variant][pixel] =
                    rgb565(sprite_tint(color_variant(instance->clock_color, variant), BLOCK_SPRITE[pixel]));
    }
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
    for (auto& instance : state->instances) if (instance.active && instance.instance_id == instance_id) return &instance;
    return nullptr;
}

InstanceState* create_instance(PackageState* state, uint32_t instance_id) {
    for (auto& instance : state->instances) if (!instance.active) {
        instance = {};
        instance.active = true;
        instance.instance_id = instance_id;
        instance.decision_state = instance_id ^ 0x9E3779B9u;
        return &instance;
    }
    return nullptr;
}

uint32_t parse_shift_interval(const char* json) {
    if (!json) return DEFAULT_SHIFT_MINUTES * 60000u;
    const char* key = "burn_in_shift_minutes";
    for (const char* value = json; *value; ++value) {
        uint8_t index = 0;
        while (key[index] && value[index] == key[index]) ++index;
        if (key[index]) continue;
        while (*value && *value != ':') ++value;
        if (*value != ':') return DEFAULT_SHIFT_MINUTES * 60000u;
        ++value;
        while (*value == ' ' || *value == '\t') ++value;
        uint32_t minutes = 0;
        while (*value >= '0' && *value <= '9') minutes = minutes * 10u + static_cast<uint32_t>(*value++ - '0');
        return minutes * 60000u;
    }
    return DEFAULT_SHIFT_MINUTES * 60000u;
}

uint16_t parse_speed_percent(const char* json) {
    if (!json) return 100;
    const char* key = "speed";
    for (const char* value = json; *value; ++value) {
        uint8_t index = 0;
        while (key[index] && value[index] == key[index]) ++index;
        if (key[index]) continue;
        while (*value && *value != ':') ++value;
        if (*value++ != ':') return 100;
        while (*value == ' ' || *value == '\t') ++value;
        uint16_t whole = 0;
        while (*value >= '0' && *value <= '9') whole = static_cast<uint16_t>(whole * 10u + *value++ - '0');
        uint16_t fraction = 0;
        if (*value == '.') {
            ++value;
            if (*value >= '0' && *value <= '9') fraction = static_cast<uint16_t>((*value++ - '0') * 10u);
            if (*value >= '0' && *value <= '9') fraction += static_cast<uint16_t>(*value - '0');
        }
        uint16_t percent = static_cast<uint16_t>(whole * 100u + fraction);
        if (percent < 25) return 25;
        if (percent > 400) return 400;
        return percent;
    }
    return 100;
}

uint32_t scaled_interval(uint32_t base_ms, uint16_t speed_percent) {
    return (base_ms * 100u + speed_percent / 2u) / speed_percent;
}

bool parse_config_string(const char* json, const char* key, char* out, size_t capacity) {
    if (!json || !key || !out || capacity == 0) return false;
    for (const char* value = json; *value; ++value) {
        uint8_t index = 0;
        while (key[index] && value[index] == key[index]) ++index;
        if (key[index]) continue;
        while (*value && *value != ':') ++value;
        if (*value++ != ':') return false;
        while (*value == ' ' || *value == '\t') ++value;
        if (*value++ != '\"') return false;
        index = 0;
        while (*value && *value != '\"' && index + 1 < capacity) out[index++] = *value++;
        out[index] = '\0';
        return out[0] != '\0';
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
    return parse_config_string(json, key, value, sizeof(value)) && parse_hex_color(value, color);
}

void normalize_clock(const char* source, char* out, size_t capacity) {
    uint8_t digits = 0;
    size_t index = 0;
    for (; source && *source && index + 1 < capacity; ++source) {
        if (*source >= '0' && *source <= '9') {
            if (digits == 4) break;
            out[index++] = *source;
            ++digits;
        } else if (*source == ':' && digits && digits < 4 && out[index - 1] != ':') out[index++] = *source;
    }
    out[index] = '\0';
}

uint8_t clock_columns(const char* clock) {
    uint8_t columns = 0;
    bool previous = false;
    for (const char* value = clock; value && *value; ++value) {
        if ((*value < '0' || *value > '9') && *value != ':') continue;
        if (previous) ++columns;
        columns += *value == ':' ? 1 : DIGIT_COLUMNS;
        previous = true;
    }
    return columns ? columns : 1;
}

void draw_block(const NativeExtensionHostApi* host, InstanceState* instance, int32_t x, int32_t y, uint8_t color) {
    const int32_t size = instance->cell;
    if (x + size <= 0 || y + size <= 0 || x >= instance->width || y >= instance->height) return;
    host->canvas->canvas_blit_rgb565(instance->canvas, x, y, instance->tinted_sprite_colors[color % SPRITE_COLOR_VARIANTS],
                                     SPRITE_SIZE, SPRITE_SIZE, instance->cell, instance->cell);
    ++instance->perf_fill_calls;
}

bool clock_cell(const char* clock, uint8_t wanted_column, uint8_t row, uint8_t* color, uint8_t* character_index) {
    uint8_t column = 0;
    bool previous = false;
    for (uint8_t index = 0; clock && clock[index]; ++index) {
        const char character = clock[index];
        if ((character < '0' || character > '9') && character != ':') continue;
        if (previous) ++column;
        if (character == ':') {
            if (wanted_column == column && (row == 1 || row == 3)) {
                *color = 1;
                *character_index = index;
                return true;
            }
            ++column;
        } else {
            const uint8_t digit = static_cast<uint8_t>(character - '0');
            for (uint8_t digit_column = 0; digit_column < DIGIT_COLUMNS; ++digit_column) {
                if (wanted_column != column + digit_column) continue;
                if (DIGITS[digit][row] & (1u << (DIGIT_COLUMNS - digit_column - 1))) {
                    *color = static_cast<uint8_t>((column + row + digit_column) % 7);
                    *character_index = index;
                    return true;
                }
            }
            column += DIGIT_COLUMNS;
        }
        previous = true;
    }
    return false;
}

int16_t clock_x(const InstanceState* instance, const char* clock, int8_t shift_x) {
    const int16_t grid_x = (static_cast<int16_t>(instance->width) - MAX_CLOCK_COLUMNS * instance->cell) / 2;
    return static_cast<int16_t>(grid_x + (MAX_CLOCK_COLUMNS - clock_columns(clock)) / 2 * instance->cell + shift_x * instance->cell);
}

int16_t clock_y(const InstanceState* instance, int8_t shift_y) {
    return static_cast<int16_t>((instance->grid_rows / 4 - DIGIT_ROWS / 2 + shift_y) * instance->cell);
}

void clear_complete_rows(InstanceState* instance) {
    for (int8_t row = GAME_ROWS - 1; row >= 0; --row) {
        bool full = true;
        for (uint8_t column = 0; column < instance->game_columns; ++column) if (!instance->game[row][column]) full = false;
        if (!full) continue;
        for (int8_t above = row; above > 0; --above)
            for (uint8_t column = 0; column < instance->game_columns; ++column)
                instance->game[above][column] = instance->game[above - 1][column];
        for (uint8_t column = 0; column < instance->game_columns; ++column) instance->game[0][column] = 0;
        ++row;
    }
}

void clear_bottom_row(InstanceState* instance) {
    for (int8_t row = GAME_ROWS - 1; row > 0; --row)
        for (uint8_t column = 0; column < instance->game_columns; ++column)
            instance->game[row][column] = instance->game[row - 1][column];
    for (uint8_t column = 0; column < instance->game_columns; ++column) instance->game[0][column] = 0;
}

bool stack_is_too_high(const InstanceState* instance) {
    for (uint8_t row = 0; row < GAME_ROWS - MAX_SAFE_GAME_HEIGHT; ++row)
        for (uint8_t column = 0; column < instance->game_columns; ++column)
            if (instance->game[row][column]) return true;
    return false;
}

int8_t raw_piece_cell_x(uint8_t shape, uint8_t rotation, uint8_t index) {
    const int8_t x = SHAPES[shape][index][0];
    const int8_t y = SHAPES[shape][index][1];
    if (rotation == 1) return y;
    if (rotation == 2) return 3 - x;
    if (rotation == 3) return 3 - y;
    return x;
}

int8_t raw_piece_cell_y(uint8_t shape, uint8_t rotation, uint8_t index) {
    const int8_t x = SHAPES[shape][index][0];
    const int8_t y = SHAPES[shape][index][1];
    if (rotation == 1) return 3 - x;
    if (rotation == 2) return 3 - y;
    if (rotation == 3) return x;
    return y;
}

int8_t piece_cell_x(uint8_t shape, uint8_t rotation, uint8_t index) {
    int8_t minimum = raw_piece_cell_x(shape, rotation, 0);
    for (uint8_t cell = 1; cell < 4; ++cell) {
        const int8_t value = raw_piece_cell_x(shape, rotation, cell);
        if (value < minimum) minimum = value;
    }
    return raw_piece_cell_x(shape, rotation, index) - minimum;
}

int8_t piece_cell_y(uint8_t shape, uint8_t rotation, uint8_t index) {
    int8_t minimum = raw_piece_cell_y(shape, rotation, 0);
    for (uint8_t cell = 1; cell < 4; ++cell) {
        const int8_t value = raw_piece_cell_y(shape, rotation, cell);
        if (value < minimum) minimum = value;
    }
    return raw_piece_cell_y(shape, rotation, index) - minimum;
}

uint8_t shape_width(uint8_t shape, uint8_t rotation) {
    uint8_t width = 0;
    for (uint8_t index = 0; index < 4; ++index) {
        const uint8_t right = static_cast<uint8_t>(piece_cell_x(shape, rotation, index) + 1);
        if (right > width) width = right;
    }
    return width;
}

bool game_collides(const InstanceState* instance, const GamePiece& piece, int8_t offset_y) {
    for (uint8_t index = 0; index < 4; ++index) {
        const int8_t column = piece.x + piece_cell_x(piece.shape, piece.rotation, index);
        const int8_t row = piece.y + piece_cell_y(piece.shape, piece.rotation, index) + offset_y - instance->game_top;
        if (column < 0 || column >= instance->game_columns || row >= GAME_ROWS) return true;
        if (row >= 0 && instance->game[row][column]) return true;
    }
    return false;
}

int16_t placement_score(const InstanceState* instance, uint8_t shape, uint8_t rotation, int8_t column) {
    GamePiece candidate = {};
    candidate.active = true;
    candidate.shape = shape;
    candidate.color = shape;
    candidate.rotation = rotation;
    candidate.target_rotation = rotation;
    candidate.x = column;
    candidate.target_x = column;
    candidate.y = -4;
    while (!game_collides(instance, candidate, 1)) ++candidate.y;
    uint8_t board[GAME_ROWS][MAX_GAME_COLUMNS] = {};
    for (uint8_t row = 0; row < GAME_ROWS; ++row)
        for (uint8_t board_column = 0; board_column < instance->game_columns; ++board_column)
            board[row][board_column] = instance->game[row][board_column];
    for (uint8_t cell = 0; cell < 4; ++cell) {
        const int8_t row = candidate.y + piece_cell_y(shape, rotation, cell) - instance->game_top;
        const int8_t board_column = candidate.x + piece_cell_x(shape, rotation, cell);
        if (row >= 0 && row < GAME_ROWS && board_column >= 0 && board_column < instance->game_columns)
            board[row][board_column] = 1;
    }
    uint8_t lines = 0;
    for (int8_t row = GAME_ROWS - 1; row >= 0; --row) {
        bool full = true;
        for (uint8_t board_column = 0; board_column < instance->game_columns; ++board_column)
            if (!board[row][board_column]) full = false;
        if (!full) continue;
        ++lines;
        for (int8_t above = row; above > 0; --above)
            for (uint8_t board_column = 0; board_column < instance->game_columns; ++board_column)
                board[above][board_column] = board[above - 1][board_column];
        for (uint8_t board_column = 0; board_column < instance->game_columns; ++board_column) board[0][board_column] = 0;
        ++row;
    }
    uint16_t holes = 0;
    uint16_t aggregate_height = 0;
    uint16_t bumpiness = 0;
    uint8_t max_height = 0;
    uint8_t near_complete = 0;
    uint8_t previous_height = 0;
    for (uint8_t board_column = 0; board_column < instance->game_columns; ++board_column) {
        bool seen_block = false;
        uint8_t column_height = 0;
        for (uint8_t row = 0; row < GAME_ROWS; ++row) {
            const bool filled = board[row][board_column] != 0;
            if (filled && !seen_block) column_height = GAME_ROWS - row;
            if (filled) seen_block = true;
            else if (seen_block) ++holes;
        }
        aggregate_height += column_height;
        if (column_height > max_height) max_height = column_height;
        if (board_column) bumpiness += column_height > previous_height ? column_height - previous_height : previous_height - column_height;
        previous_height = column_height;
    }
    for (uint8_t row = 0; row < GAME_ROWS; ++row) {
        uint8_t filled = 0;
        for (uint8_t board_column = 0; board_column < instance->game_columns; ++board_column)
            if (board[row][board_column]) ++filled;
        if (filled + 1 >= instance->game_columns) ++near_complete;
    }
    return static_cast<int16_t>(lines * 20000 + near_complete * 350 - holes * 500 -
                                aggregate_height * 35 - bumpiness * 80 - max_height * 180);
}

void plan_game_piece(const InstanceState* instance, uint8_t shape, uint8_t* rotation, int8_t* column) {
    *rotation = 0;
    *column = 0;
    int16_t best_score = -32767;
    for (uint8_t candidate_rotation = 0; candidate_rotation < 4; ++candidate_rotation) {
        const uint8_t width = shape_width(shape, candidate_rotation);
        if (width > instance->game_columns) continue;
        for (int8_t candidate_column = 0; candidate_column <= instance->game_columns - width; ++candidate_column) {
            const int16_t score = placement_score(instance, shape, candidate_rotation, candidate_column);
            if (score > best_score) {
                best_score = score;
                *rotation = candidate_rotation;
                *column = candidate_column;
            }
        }
    }
}

void spawn_game_piece(InstanceState* instance) {
    GamePiece& piece = instance->game_piece;
    piece = {};
    piece.active = true;
    piece.shape = instance->shape_index++ % 7;
    piece.color = piece.shape;
    plan_game_piece(instance, piece.shape, &piece.target_rotation, &piece.target_x);
    instance->decision_state = instance->decision_state * 1664525u + 1013904223u;
    if ((instance->decision_state & 3u) == 0) piece.target_rotation = 0;
    piece.rotation = 0;
    piece.x = static_cast<int8_t>((instance->game_columns - shape_width(piece.shape, piece.rotation)) / 2);
    piece.y = -2;
}

void advance_game(InstanceState* instance) {
    GamePiece& piece = instance->game_piece;
    if (!piece.active) { spawn_game_piece(instance); mark_game_dirty(instance); return; }
    const int8_t clock_top = static_cast<int8_t>(instance->grid_rows / 4 - DIGIT_ROWS / 2 + instance->shift_y);
    const bool crosses_clock = piece.y >= clock_top - 1 && piece.y < clock_top + DIGIT_ROWS;
    if (!crosses_clock && piece.rotation != piece.target_rotation && piece.y >= ROTATE_DECISION_ROW + static_cast<int8_t>(instance->decision_state & 1u)) {
        GamePiece rotated = piece;
        rotated.rotation = static_cast<uint8_t>((piece.rotation + 1) % 4);
        if (!game_collides(instance, rotated, 0)) {
            piece.rotation = rotated.rotation;
            mark_game_dirty(instance);
            return;
        }
    }
    if (!crosses_clock && piece.x != piece.target_x && piece.y >= MOVE_DECISION_ROW + static_cast<int8_t>((instance->decision_state >> 2) & 1u)) {
        const int8_t direction = piece.target_x > piece.x ? 1 : -1;
        GamePiece moved = piece;
        moved.x += direction;
        if (!game_collides(instance, moved, 0)) { piece.x = moved.x; mark_game_dirty(instance); return; }
    }
    if (!game_collides(instance, piece, 1)) { ++piece.y; mark_game_dirty(instance); return; }
    for (uint8_t index = 0; index < 4; ++index) {
        const int8_t column = piece.x + piece_cell_x(piece.shape, piece.rotation, index);
        const int8_t row = piece.y + piece_cell_y(piece.shape, piece.rotation, index) - instance->game_top;
        if (row >= 0 && row < GAME_ROWS && column >= 0 && column < instance->game_columns) instance->game[row][column] = piece.color + 1;
    }
    clear_complete_rows(instance);
    if (stack_is_too_high(instance)) clear_bottom_row(instance);
    instance->board_dirty = true;
    spawn_game_piece(instance);
    mark_game_dirty(instance);
}

void draw_clock(const NativeExtensionHostApi* host, InstanceState* instance) {
    const int16_t base_x = clock_x(instance, instance->clock, instance->shift_x);
    const int16_t base_y = clock_y(instance, instance->shift_y);
    for (uint8_t row = 0; row < DIGIT_ROWS; ++row) for (uint8_t column = 0; column < MAX_CLOCK_COLUMNS; ++column) {
        uint8_t color = 0, character = 0;
        if (!clock_cell(instance->clock, column, row, &color, &character)) continue;
        if (instance->clock_color_set) color = CUSTOM_CLOCK_COLOR_INDEX + color;
        draw_block(host, instance, base_x + column * instance->cell, base_y + row * instance->cell, color);
    }
}

int16_t game_x(const InstanceState* instance) {
    return (static_cast<int16_t>(instance->width) - MAX_CLOCK_COLUMNS * instance->cell) / 2;
}

void draw_board(const NativeExtensionHostApi* host, InstanceState* instance) {
    const int16_t left = game_x(instance);
    for (uint8_t row = 0; row < GAME_ROWS; ++row) for (uint8_t column = 0; column < instance->game_columns; ++column)
        if (instance->game[row][column]) draw_block(host, instance, left + column * instance->cell,
                                                     (instance->game_top + row) * instance->cell,
                                                     instance->block_color_set ? CUSTOM_BLOCK_COLOR_INDEX + (instance->game[row][column] - 1) % DEFAULT_COLOR_VARIANTS : instance->game[row][column] - 1);
}

void draw_piece(const NativeExtensionHostApi* host, InstanceState* instance, const GamePiece& piece) {
    if (!piece.active) return;
    const int16_t left = game_x(instance);
    for (uint8_t index = 0; index < 4; ++index)
        draw_block(host, instance, left + (piece.x + piece_cell_x(piece.shape, piece.rotation, index)) * instance->cell,
                   (piece.y + piece_cell_y(piece.shape, piece.rotation, index)) * instance->cell,
                   instance->block_color_set ? CUSTOM_BLOCK_COLOR_INDEX + piece.color % DEFAULT_COLOR_VARIANTS : piece.color);
}

void erase_piece(const NativeExtensionHostApi* host, InstanceState* instance, const GamePiece& piece, uint32_t background) {
    if (!piece.active) return;
    const int16_t left = game_x(instance);
    for (uint8_t index = 0; index < 4; ++index)
        host->canvas->canvas_fill_rect(instance->canvas,
                                       left + (piece.x + piece_cell_x(piece.shape, piece.rotation, index)) * instance->cell,
                                       (piece.y + piece_cell_y(piece.shape, piece.rotation, index)) * instance->cell,
                                       instance->cell, instance->cell, background);
}

bool piece_overlaps_clock(const InstanceState* instance, const GamePiece& piece) {
    if (!piece.active) return false;
    const int8_t clock_top = static_cast<int8_t>(instance->grid_rows / 4 - DIGIT_ROWS / 2 + instance->shift_y);
    for (uint8_t index = 0; index < 4; ++index) {
        const int8_t row = piece.y + piece_cell_y(piece.shape, piece.rotation, index);
        if (row >= clock_top && row < clock_top + DIGIT_ROWS) return true;
    }
    return false;
}

void render(const NativeExtensionHostApi* host, InstanceState* instance) {
    NativeExtensionButtonSnapshot button = {};
    uint32_t background = 0x143D52;
    if (host->button && host->button->get(instance->extension_context, instance->instance_id, &button)) background = button.background_rgb;
    const uint32_t clear_started_ms = host->core->millis();
    if (instance->full_render) {
        host->canvas->canvas_clear(instance->canvas, background);
    } else {
        if (instance->board_dirty)
            host->canvas->canvas_fill_rect(instance->canvas, 0, instance->game_top * instance->cell,
                                            instance->width, GAME_ROWS * instance->cell, background);
        else if (instance->game_dirty && instance->rendered_piece_valid)
            erase_piece(host, instance, instance->rendered_piece, background);
        if (instance->clock_dirty) {
            const int32_t top = (static_cast<int32_t>(instance->grid_rows) / 4 - DIGIT_ROWS / 2 - 1) * instance->cell;
            const int32_t height = (DIGIT_ROWS + 2) * instance->cell;
            host->canvas->canvas_fill_rect(instance->canvas, 0, top < 0 ? 0 : top, instance->width, height, background);
        }
    }
    instance->perf_clear_total_ms += host->core->millis() - clear_started_ms;

    const uint32_t draw_started_ms = host->core->millis();
    const bool refresh_clock_after_piece = instance->game_dirty &&
                                           (piece_overlaps_clock(instance, instance->rendered_piece) ||
                                            piece_overlaps_clock(instance, instance->game_piece));
    if (instance->full_render || instance->board_dirty) draw_board(host, instance);
    if (instance->full_render || instance->game_dirty) draw_piece(host, instance, instance->game_piece);
    if (instance->full_render || instance->clock_dirty || refresh_clock_after_piece) draw_clock(host, instance);

    if (instance->full_render) {
        host->canvas->canvas_invalidate_rect(instance->canvas, 0, 0, instance->width, instance->height);
    } else {
        if (instance->board_dirty)
            host->canvas->canvas_invalidate_rect(instance->canvas, 0, instance->game_top * instance->cell,
                                                  instance->width, GAME_ROWS * instance->cell);
        else if (instance->game_dirty)
            host->canvas->canvas_invalidate_rect(instance->canvas, 0, 0, instance->width, instance->height);
        if (instance->clock_dirty) {
            const int32_t top = (static_cast<int32_t>(instance->grid_rows) / 4 - DIGIT_ROWS / 2 - 1) * instance->cell;
            const int32_t height = (DIGIT_ROWS + 2) * instance->cell;
            host->canvas->canvas_invalidate_rect(instance->canvas, 0, top < 0 ? 0 : top, instance->width, height);
        }
    }
    instance->perf_draw_total_ms += host->core->millis() - draw_started_ms;
    instance->render_dirty = false;
    instance->game_dirty = false;
    instance->clock_dirty = false;
    instance->full_render = false;
    instance->board_dirty = false;
    instance->rendered_piece = instance->game_piece;
    instance->rendered_piece_valid = instance->game_piece.active;
}

} // namespace

extern "C" void native_extension_create_instance(const NativeExtensionHostApi* host, void* extension_context,
                                                  uint32_t instance_id, void* root, const char* config_json) {
    if (!host || !host->core || !host->ui || !host->canvas || !host->binding || !root) return;
    PackageState* state = package_state(host, extension_context);
    InstanceState* instance = state ? create_instance(state, instance_id) : nullptr;
    if (!instance) return;
    instance->extension_context = extension_context;
    instance->width = static_cast<uint16_t>(host->ui->obj_get_width(root));
    instance->height = static_cast<uint16_t>(host->ui->obj_get_height(root));
    copy_text(instance->time_template, sizeof(instance->time_template), "[time:%H%M]");
    parse_config_string(config_json, "time", instance->time_template, sizeof(instance->time_template));
    instance->shift_interval_ms = parse_shift_interval(config_json);
    instance->speed_percent = parse_speed_percent(config_json);
    instance->block_color_set = parse_color_config(config_json, "block_color", &instance->block_color);
    instance->clock_color_set = parse_color_config(config_json, "clock_color", &instance->clock_color);
    char resolved[CLOCK_TEXT_MAX_LEN] = {};
    if (!host->binding->resolve(extension_context, instance_id, instance->time_template, resolved, sizeof(resolved))) return;
    normalize_clock(resolved, instance->clock, sizeof(instance->clock));
    if (!instance->clock[0]) copy_text(instance->clock, sizeof(instance->clock), "0000");
    instance->cell = static_cast<uint16_t>(instance->width / MAX_CLOCK_COLUMNS);
    const uint16_t vertical_cell = static_cast<uint16_t>(instance->height / 16u);
    if (vertical_cell < instance->cell) instance->cell = vertical_cell;
    if (instance->cell < 3) { instance->active = false; return; }
    instance->grid_rows = static_cast<uint8_t>(instance->height / instance->cell);
    instance->game_top = instance->grid_rows - GAME_ROWS;
    instance->game_columns = MAX_CLOCK_COLUMNS;
    if (instance->game_columns < MIN_GAME_COLUMNS) instance->game_columns = MIN_GAME_COLUMNS;
    instance->canvas = host->canvas->canvas_create(root);
    instance->buffer = host->core->alloc(host->canvas->canvas_buffer_size(instance->width, instance->height));
    if (!instance->canvas || !instance->buffer) { instance->active = false; return; }
    host->canvas->canvas_set_buffer(instance->canvas, instance->buffer, instance->width, instance->height);
    build_tinted_sprite_colors(instance);
    spawn_game_piece(instance);
    instance->render_dirty = true;
    instance->full_render = true;
    instance->last_shift_ms = host->core->millis();
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
    if (!host || !host->core || !host->binding) return;
    PackageState* state = static_cast<PackageState*>(host->core->context_get_data(extension_context));
    InstanceState* instance = state ? find_instance(state, instance_id) : nullptr;
    if (!instance) return;
    const uint32_t tick_started_ms = host->core->millis();
    const uint32_t now = tick_started_ms;
    ++instance->perf_tick_count;
    if (now >= instance->next_game_step_ms) {
        advance_game(instance);
        instance->perf_game_interval_ms = static_cast<uint16_t>(scaled_interval(GAME_FALL_MS, instance->speed_percent));
        instance->next_game_step_ms = now + instance->perf_game_interval_ms;
    }
    if (now >= instance->next_clock_resolve_ms) {
        char resolved[CLOCK_TEXT_MAX_LEN] = {};
        char clock[CLOCK_TEXT_MAX_LEN] = {};
        if (host->binding->resolve(extension_context, instance_id, instance->time_template, resolved, sizeof(resolved))) normalize_clock(resolved, clock, sizeof(clock));
        if (clock[0] && !text_equals(instance->clock, clock)) {
            copy_text(instance->clock, sizeof(instance->clock), clock);
            mark_clock_dirty(instance);
            ++instance->perf_clock_dirty_count;
        }
        instance->next_clock_resolve_ms = now + CLOCK_RESOLVE_MS;
    }
    if (instance->shift_interval_ms && now - instance->last_shift_ms >= instance->shift_interval_ms) {
        instance->shift_y = instance->shift_y == 1 ? -1 : instance->shift_y + 1;
        instance->last_shift_ms = now;
        mark_clock_dirty(instance);
        ++instance->perf_shift_dirty_count;
    }
    if (instance->render_dirty) {
        const uint32_t render_started_ms = host->core->millis();
        render(host, instance);
        instance->perf_render_total_ms += host->core->millis() - render_started_ms;
        ++instance->perf_render_count;
    }
    instance->perf_tick_total_ms += host->core->millis() - tick_started_ms;
    log_performance(host, instance, now);
}

extern "C" NativeExtensionEventResult native_extension_on_tap(const NativeExtensionHostApi*, void*, uint32_t) {
    return NATIVE_EXTENSION_PASS_THROUGH;
}

extern "C" NativeExtensionEventResult native_extension_on_long_press(const NativeExtensionHostApi*, void*, uint32_t) {
    return NATIVE_EXTENSION_PASS_THROUGH;
}
