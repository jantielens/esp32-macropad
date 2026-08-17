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
constexpr uint32_t BACKGROUND_RGB = 0x010603;
constexpr uint32_t CLOCK_RESOLVE_MS = 500;
constexpr uint32_t CLOCK_UPDATE_FAILSAFE_MS = 10000;
constexpr uint32_t CLOCK_MINUTE_MS = 60000;
constexpr uint32_t CLOCK_LAUNCH_MARGIN_MS = 1000;
constexpr uint8_t CLOCK_CHARACTERS = 5;
constexpr uint8_t CLOCK_TEXT_CAPACITY = CLOCK_CHARACTERS + 1;
constexpr uint8_t CLOCK_TEMPLATE_CAPACITY = 96;
constexpr uint32_t CLOCK_RGB = 0x00FF41;
constexpr int32_t GLYPH_CLEAR_PAD_X = 2;
constexpr int32_t GLYPH_CLEAR_PAD_Y = 4;
constexpr char CHARACTER_SET[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+-*/=<>[]{}:;|\\";

struct Column {
    float head;
    float speed;
    uint8_t length;
    uint32_t random;
};

struct ClockLane {
    uint32_t target_ms;
    uint8_t planned;
    uint8_t launched;
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
    char resolved_clock[CLOCK_TEXT_CAPACITY];
    char pending_clock[CLOCK_TEXT_CAPACITY];
    char planned_clock[CLOCK_TEXT_CAPACITY];
    ClockLane clock_lanes[CLOCK_CHARACTERS];
    uint32_t pending_clock_since_ms[CLOCK_CHARACTERS];
    uint32_t next_clock_resolve_ms;
    uint32_t next_minute_ms;
    uint16_t planned_minute;
    uint8_t clock_enabled;
    uint8_t clock_seconds;
    bool clock_seconds_valid;
    bool clock_plan_needed;
    uint8_t clock_column;
    uint8_t clock_row;
    uint8_t clock_hit_mask;
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

uint32_t green(uint8_t level) {
    return (static_cast<uint32_t>(level / 5) << 16) | (static_cast<uint32_t>(level) << 8) | (static_cast<uint32_t>(level / 8));
}

bool text_equals(const char* left, const char* right) {
    size_t index = 0;
    while (left[index] && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

bool normalize_clock(const char* source, char* out, size_t capacity, uint8_t* seconds, bool* seconds_valid) {
    if (!source || !out || capacity < CLOCK_TEXT_CAPACITY) return false;
    char digits[6];
    uint8_t count = 0;
    for (; *source && count < sizeof(digits); ++source)
        if (*source >= '0' && *source <= '9') digits[count++] = *source;
    if (count < 4) return false;
    out[0] = digits[0];
    out[1] = digits[1];
    out[2] = ':';
    out[3] = digits[2];
    out[4] = digits[3];
    out[5] = '\0';
    if (seconds_valid) *seconds_valid = count >= 6;
    if (seconds && count >= 6) *seconds = static_cast<uint8_t>((digits[4] - '0') * 10 + digits[5] - '0');
    return true;
}

void reset_column(InstanceState* instance, Column* column, uint8_t index) {
    column->random = next_random(instance);
    const uint8_t base_length = static_cast<uint8_t>(5 + column->random % (instance->rows > 10 ? instance->rows / 2 : 5));
    const uint8_t scaled_length = static_cast<uint8_t>(base_length * instance->trail_length);
    column->length = scaled_length == 0 ? 1 : (scaled_length > instance->rows ? instance->rows : scaled_length);
    column->head = -static_cast<float>(column->random % (instance->rows + column->length));
    column->speed = (0.036f + static_cast<float>(column->random % 12) * 0.006f) * instance->speed;
    if (static_cast<float>(column->random % 1000) / 1000.0f > instance->density) column->speed = 0.0f;
    (void)index;
}

bool is_clock_digit(uint8_t index) {
    return index != 2;
}

void next_minute_clock(const char* clock, char* next) {
    uint16_t minutes = static_cast<uint16_t>((clock[0] - '0') * 600 + (clock[1] - '0') * 60 +
                                              (clock[3] - '0') * 10 + (clock[4] - '0'));
    minutes = static_cast<uint16_t>((minutes + 1u) % (24u * 60u));
    next[0] = static_cast<char>('0' + minutes / 600u);
    next[1] = static_cast<char>('0' + (minutes / 60u) % 10u);
    next[2] = ':';
    next[3] = static_cast<char>('0' + (minutes % 60u) / 10u);
    next[4] = static_cast<char>('0' + minutes % 10u);
    next[5] = '\0';
}

uint32_t column_clear_ms(const InstanceState* instance, const Column& drop) {
    if (drop.speed <= 0.0f || drop.head - drop.length > instance->rows) return 0;
    const float cells = static_cast<float>(instance->rows + drop.length) - drop.head;
    return cells > 0.0f ? static_cast<uint32_t>(cells * 100.0f / drop.speed) : 0;
}

uint32_t first_reachable_minute(uint32_t now, uint32_t next_minute_ms, uint32_t clear_ms) {
    uint32_t target = next_minute_ms;
    while (static_cast<int32_t>(target - now) < static_cast<int32_t>(clear_ms + CLOCK_LAUNCH_MARGIN_MS))
        target += CLOCK_MINUTE_MS;
    return target;
}

void reset_clock_lane(InstanceState* instance, uint8_t index, uint32_t now) {
    const uint8_t column = static_cast<uint8_t>(instance->clock_column + index);
    ClockLane& lane = instance->clock_lanes[index];
    Column& drop = instance->rain[column];
    reset_column(instance, &drop, column);
    if (!lane.planned) return;
    while (static_cast<int32_t>(lane.target_ms - now) < 0)
        lane.target_ms += CLOCK_MINUTE_MS;
    if (drop.speed == 0.0f) drop.speed = 0.036f * instance->speed;
    drop.head = static_cast<float>(instance->clock_row) -
                drop.speed * static_cast<float>(lane.target_ms - now) / 100.0f;
    lane.launched = 1;
}

void queue_clock_update(InstanceState* instance, const char* clock, uint32_t now) {
    if (text_equals(instance->clock, clock)) return;
    for (uint8_t index = 0; index < CLOCK_CHARACTERS; ++index) {
        if (static_cast<int32_t>(now - instance->next_minute_ms) < 0 &&
            instance->pending_clock[index] == instance->planned_clock[index])
            continue;
        if (instance->clock[index] == clock[index]) {
            instance->pending_clock[index] = '\0';
            continue;
        }
        if (instance->pending_clock[index] == clock[index]) {
            if (instance->pending_clock_since_ms[index] == 0) instance->pending_clock_since_ms[index] = now;
            continue;
        }
        instance->pending_clock[index] = clock[index];
        instance->pending_clock_since_ms[index] = now;
    }
    instance->pending_clock[CLOCK_CHARACTERS] = '\0';
}

void resolve_clock(const NativeExtensionHostApi* host, InstanceState* instance, uint32_t now) {
    char resolved[CLOCK_TEMPLATE_CAPACITY] = {};
    char clock[CLOCK_TEXT_CAPACITY] = {};
    if (!host->binding->resolve(instance->extension_context, instance->instance_id,
                                instance->clock_template, resolved, sizeof(resolved)) ||
        !normalize_clock(resolved, clock, sizeof(clock), &instance->clock_seconds,
                 &instance->clock_seconds_valid)) return;
    const uint16_t minute = static_cast<uint16_t>((clock[0] - '0') * 600 + (clock[1] - '0') * 60 +
                                                   (clock[3] - '0') * 10 + (clock[4] - '0'));
    const bool minute_changed = instance->clock_seconds_valid && minute != instance->planned_minute;
    copy_text(instance->resolved_clock, sizeof(instance->resolved_clock), clock);
    queue_clock_update(instance, clock, now);
    if (minute_changed) {
        instance->planned_minute = minute;
        instance->next_minute_ms = now + static_cast<uint32_t>(60u - instance->clock_seconds) * 1000u;
        instance->clock_plan_needed = true;
    }
}

void plan_clock_lanes(InstanceState* instance, uint32_t now) {
    if (!instance->clock_plan_needed || !instance->clock_seconds_valid) return;
    next_minute_clock(instance->resolved_clock, instance->planned_clock);
    for (uint8_t index = 0; index < CLOCK_CHARACTERS; ++index) {
        if (!is_clock_digit(index)) continue;
        if (instance->clock[index] == instance->planned_clock[index]) continue;
        const uint8_t column = static_cast<uint8_t>(instance->clock_column + index);
        ClockLane& lane = instance->clock_lanes[index];
        lane.target_ms = first_reachable_minute(now, instance->next_minute_ms,
                                                column_clear_ms(instance, instance->rain[column]));
        lane.planned = 1;
        lane.launched = 0;
    }
    instance->clock_plan_needed = false;
}

void apply_clock_failsafe(InstanceState* instance, uint32_t now) {
    for (uint8_t index = 0; index < CLOCK_CHARACTERS; ++index) {
        if (!instance->pending_clock[index] || !instance->pending_clock_since_ms[index] ||
            now - instance->pending_clock_since_ms[index] < CLOCK_UPDATE_FAILSAFE_MS)
            continue;
        instance->clock[index] = instance->pending_clock[index];
        instance->pending_clock[index] = '\0';
        instance->pending_clock_since_ms[index] = 0;
    }
}

void update_clock_on_hit(InstanceState* instance, uint8_t column, float previous_head, float current_head) {
    if (previous_head > instance->clock_row || current_head < instance->clock_row) return;
    if (column < instance->clock_column || column >= instance->clock_column + CLOCK_CHARACTERS) return;
    const uint8_t index = static_cast<uint8_t>(column - instance->clock_column);
    const bool planned_hit = instance->clock_lanes[index].planned && instance->clock_lanes[index].launched;
    if (!instance->pending_clock[index]) {
        if (planned_hit) {
            instance->clock_lanes[index].planned = 0;
            instance->clock_lanes[index].launched = 0;
        }
        return;
    }
    instance->clock[index] = instance->pending_clock[index];
    instance->pending_clock[index] = '\0';
    instance->pending_clock_since_ms[index] = 0;
    instance->clock_hit_mask |= static_cast<uint8_t>(1u << index);
    if (planned_hit) {
        instance->clock_lanes[index].planned = 0;
        instance->clock_lanes[index].launched = 0;
    }
}

void draw_clock(const NativeExtensionHostApi* host, const InstanceState* instance) {
    if (!instance->clock_enabled) return;
    char character[2] = {0, '\0'};
    for (uint8_t index = 0; index < CLOCK_CHARACTERS; ++index) {
        const int32_t x = (instance->clock_column + index) * instance->cell_width;
        const int32_t y = instance->clock_row * instance->cell_height;
        if (instance->clock_hit_mask & static_cast<uint8_t>(1u << index)) continue;
        character[0] = instance->clock[index];
        host->canvas->canvas_draw_text(instance->canvas, x, y, character,
                                       instance->font_name, instance->font_size, CLOCK_RGB);
    }
}

void clear_clock(const NativeExtensionHostApi* host, const InstanceState* instance) {
    if (!instance->clock_enabled) return;
    for (uint8_t index = 0; index < CLOCK_CHARACTERS; ++index)
        host->canvas->canvas_fill_rect(instance->canvas,
                                       (instance->clock_column + index) * instance->cell_width - GLYPH_CLEAR_PAD_X,
                                       instance->clock_row * instance->cell_height - GLYPH_CLEAR_PAD_Y,
                                       instance->cell_width + GLYPH_CLEAR_PAD_X * 2,
                                       instance->cell_height + GLYPH_CLEAR_PAD_Y * 2, BACKGROUND_RGB);
}

uint16_t render(const NativeExtensionHostApi* host, InstanceState* instance) {
    if (!instance->background_initialized) {
        host->canvas->canvas_fill_rect(instance->canvas, 0, 0, instance->width, instance->height, BACKGROUND_RGB);
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
                                                    BACKGROUND_RGB);
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
            const bool clock_cell = instance->clock_enabled && row == instance->clock_row &&
                column >= instance->clock_column && column < instance->clock_column + CLOCK_CHARACTERS;
            const uint8_t clock_index = clock_cell ? static_cast<uint8_t>(column - instance->clock_column) : 0;
            if (clock_cell && (distance >= 1.0f ||
                               !(instance->clock_hit_mask & static_cast<uint8_t>(1u << clock_index)))) continue;
            uint8_t intensity = static_cast<uint8_t>(255.0f * (1.0f - distance / drop.length));
            if (distance < 1.0f) intensity = 255;
            glyph[0] = CHARACTER_SET[(drop.random + row * 17u + column * 31u) % (sizeof(CHARACTER_SET) - 1)];
            if (clock_cell) glyph[0] = instance->clock[clock_index];
            host->canvas->canvas_draw_text(instance->canvas, column * instance->cell_width,
                                            row * instance->cell_height, glyph, instance->font_name,
                                            instance->font_size, green(intensity));
            instance->active_cells[column][row / 32] |= 1u << (row % 32);
            ++rendered_glyphs;
            if (++rendered_for_column >= glyphs_per_column) break;
        }
    }
    draw_clock(host, instance);
    host->canvas->canvas_invalidate_rect(instance->canvas, 0, 0, instance->width, instance->height);
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
                normalize_clock(resolved, instance->clock, sizeof(instance->clock),
                                &instance->clock_seconds, &instance->clock_seconds_valid);
            copy_text(instance->resolved_clock, sizeof(instance->resolved_clock), instance->clock);
            instance->planned_minute = 0xFFFFu;
            instance->clock_plan_needed = false;
            if (!instance->clock[0]) copy_text(instance->clock, sizeof(instance->clock), "00:00");
        }
    }
    instance->canvas = host->canvas->canvas_create(root);
    instance->buffer = host->core->alloc(host->canvas->canvas_buffer_size(instance->width, instance->height));
    if (!instance->canvas || !instance->buffer) { instance->active = false; return; }
    host->canvas->canvas_set_buffer(instance->canvas, instance->buffer, instance->width, instance->height);
    for (uint8_t column = 0; column < instance->columns; ++column) reset_column(instance, &instance->rain[column], column);
    instance->dirty = true;
    instance->last_motion_ms = host->core->millis();
    instance->next_clock_resolve_ms = instance->last_motion_ms + CLOCK_RESOLVE_MS;
    instance->clock_hit_mask = 0;
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
    instance->clock_hit_mask = 0;
    if (instance->clock_enabled && static_cast<int32_t>(now - instance->next_clock_resolve_ms) >= 0) {
        resolve_clock(host, instance, now);
        instance->next_clock_resolve_ms = now + CLOCK_RESOLVE_MS;
    }
    plan_clock_lanes(instance, now);
    for (uint8_t column = 0; column < instance->columns; ++column) {
        Column& drop = instance->rain[column];
        if (drop.speed == 0.0f) {
            const bool clock_lane = instance->clock_enabled && column >= instance->clock_column &&
                column < instance->clock_column + CLOCK_CHARACTERS;
            const uint8_t index = clock_lane ? static_cast<uint8_t>(column - instance->clock_column) : 0;
            if (clock_lane && instance->clock_lanes[index].planned)
                reset_clock_lane(instance, index, now);
            else
                reset_column(instance, &drop, column);
            continue;
        }
        const float previous_head = drop.head;
        drop.head += drop.speed * static_cast<float>(elapsed_ms) / 100.0f;
        update_clock_on_hit(instance, column, previous_head, drop.head);
        if (drop.head - drop.length > instance->rows) {
            const bool clock_lane = instance->clock_enabled && column >= instance->clock_column &&
                column < instance->clock_column + CLOCK_CHARACTERS;
            const uint8_t index = clock_lane ? static_cast<uint8_t>(column - instance->clock_column) : 0;
            if (clock_lane && instance->clock_lanes[index].planned)
                reset_clock_lane(instance, index, now);
            else
                reset_column(instance, &drop, column);
        }
    }
    apply_clock_failsafe(instance, now);
    render(host, instance);
}

extern "C" NativeExtensionEventResult native_extension_on_tap(const NativeExtensionHostApi*, void*, uint32_t) {
    return NATIVE_EXTENSION_PASS_THROUGH;
}

extern "C" NativeExtensionEventResult native_extension_on_long_press(const NativeExtensionHostApi*, void*, uint32_t) {
    return NATIVE_EXTENSION_PASS_THROUGH;
}