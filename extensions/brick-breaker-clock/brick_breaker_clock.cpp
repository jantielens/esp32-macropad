#include "native_extension_api.h"

extern "C" const NativeExtensionDescriptor native_extension_descriptor = {
    NATIVE_EXTENSION_DESCRIPTOR_MAGIC, NATIVE_EXTENSION_ABI_VERSION,
    NATIVE_EXTENSION_TARGET_ABI, "brick-breaker-clock", "1.0.0", "Brick Breaker Clock",
    33, 0,
};

namespace {

constexpr uint8_t MAX_INSTANCES = 16;
constexpr uint8_t MAX_CLOCK_BRICKS = 64;
constexpr uint8_t MAX_LEVEL_BRICKS = 54;
constexpr uint8_t CLOCK_DIGITS = 4;
constexpr uint8_t DIGIT_ROWS = 5;
constexpr uint8_t DIGIT_COLUMNS = 3;
constexpr uint32_t CLOCK_RESOLVE_MS = 250;
constexpr uint16_t DEFAULT_RESPAWN_MS = 1000;
constexpr uint16_t PADDLE_RESPAWN_SAFETY_MS = 30000;
constexpr uint16_t DEFAULT_SPEED_PERCENT = 100;
constexpr uint16_t DEFAULT_PADDLE_ACCURACY = 96;
constexpr uint8_t CEILING_SHOT_DENOMINATOR = 3;
constexpr uint32_t DEFAULT_BACKGROUND = 0x001010;
constexpr uint32_t DEFAULT_BRICK_COLOR = 0xF81858;
constexpr uint8_t BRICK_VARIANTS = 8;
constexpr uint8_t LEVEL_COUNT = 8;
constexpr uint8_t LEVEL_COLUMNS = 18;
constexpr uint8_t LEVEL_ROWS = 3;
constexpr uint32_t LEVEL_PATTERNS[LEVEL_COUNT][LEVEL_ROWS] = {
    {0x2AAAAu, 0x15555u, 0x2AAAAu}, {0x24924u, 0x12492u, 0x24924u},
    {0x3E07Cu, 0x1C038u, 0x0E01Cu}, {0x3FC3Fu, 0x0F00Fu, 0x3FC3Fu},
    {0x24924u, 0x12492u, 0x24924u}, {0x3E07Cu, 0x1C038u, 0x0E01Cu},
    {0x38007u, 0x1C00Eu, 0x0E01Cu}, {0x2FFFDu, 0x17FFAu, 0x2FFFDu},
};
constexpr uint8_t DIGITS[10][DIGIT_ROWS] = {
    {0b111, 0b101, 0b101, 0b101, 0b111}, {0b010, 0b110, 0b010, 0b010, 0b111},
    {0b111, 0b001, 0b111, 0b100, 0b111}, {0b111, 0b001, 0b111, 0b001, 0b111},
    {0b101, 0b101, 0b111, 0b001, 0b001}, {0b111, 0b100, 0b111, 0b001, 0b111},
    {0b111, 0b100, 0b111, 0b101, 0b111}, {0b111, 0b001, 0b010, 0b010, 0b010},
    {0b111, 0b101, 0b111, 0b101, 0b111}, {0b111, 0b101, 0b111, 0b001, 0b111},
};

#include "arkanoid_sprites.inc"

struct Rect {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
};

struct Brick {
    Rect bounds;
    uint32_t respawn_at_ms;
    uint8_t variant;
    uint8_t rendered;
};

struct LevelBrick {
    Rect bounds;
    uint8_t variant;
    uint8_t present;
    uint8_t rendered;
};

struct InstanceState {
    uint8_t active;
    uint8_t render_dirty;
    uint8_t full_render;
    uint8_t brick_color_custom;
    uint8_t respawn_on_paddle;
    uint8_t brick_count;
    uint8_t level_count;
    uint8_t level_index;
    uint32_t instance_id;
    uint32_t random_state;
    void* extension_context;
    void* canvas;
    void* canvas_buffer;
    uint16_t width;
    uint16_t height;
    uint16_t brick_width;
    uint16_t brick_height;
    uint16_t respawn_ms;
    uint16_t speed_percent;
    uint16_t paddle_accuracy;
    uint32_t background_color;
    uint32_t brick_color;
    char time_template[96];
    char clock[5];
    uint32_t next_clock_resolve_ms;
    uint32_t last_step_ms;
    Brick bricks[MAX_CLOCK_BRICKS];
    LevelBrick level_bricks[MAX_LEVEL_BRICKS];
    float ball_x;
    float ball_y;
    float ball_dx;
    float ball_dy;
    float paddle_x;
    int16_t paddle_y;
    uint16_t paddle_width;
    uint16_t paddle_height;
    uint16_t ball_size;
    Rect rendered_ball;
    Rect rendered_paddle;
};

struct PackageState { InstanceState instances[MAX_INSTANCES]; };

void copy_text(char* out, size_t capacity, const char* value) {
    if (!out || !capacity) return;
    size_t index = 0;
    while (value && value[index] && index + 1 < capacity) out[index++] = value[index];
    out[index] = '\0';
}

bool text_equals(const char* left, const char* right) {
    size_t index = 0;
    while (left[index] && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

const char* find_config_value(const char* json, const char* key) {
    if (!json || !key) return nullptr;
    for (const char* value = json; *value; ++value) {
        if (*value != '\"') continue;
        ++value;
        size_t index = 0;
        while (key[index] && value[index] == key[index]) ++index;
        if (key[index] || value[index] != '\"') continue;
        value += index + 1;
        while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') ++value;
        if (*value++ != ':') continue;
        while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') ++value;
        return value;
    }
    return nullptr;
}

bool parse_config_string(const char* json, const char* key, char* out, size_t capacity) {
    const char* value = find_config_value(json, key);
    if (!value || *value++ != '\"') return false;
    size_t index = 0;
    while (*value && *value != '\"' && index + 1 < capacity) out[index++] = *value++;
    out[index] = '\0';
    return index != 0;
}

uint16_t parse_config_uint(const char* json, const char* key, uint16_t fallback, uint16_t maximum) {
    const char* value = find_config_value(json, key);
    if (!value) return fallback;
    uint32_t parsed = 0;
    uint8_t digits = 0;
    while (*value >= '0' && *value <= '9') {
        parsed = parsed * 10u + static_cast<uint32_t>(*value++ - '0');
        if (++digits > 5 || parsed > maximum) return maximum;
    }
    return digits ? static_cast<uint16_t>(parsed) : fallback;
}

bool parse_config_bool(const char* json, const char* key, bool fallback) {
    const char* value = find_config_value(json, key);
    if (!value) return fallback;
    if (value[0] == 't' && value[1] == 'r' && value[2] == 'u' && value[3] == 'e') return true;
    if (value[0] == 'f' && value[1] == 'a' && value[2] == 'l' && value[3] == 's' && value[4] == 'e') return false;
    return fallback;
}

bool parse_hex_color(const char* value, uint32_t* color) {
    if (!value || !color || *value++ != '\"') return false;
    if (*value == '#') ++value;
    uint32_t parsed = 0;
    for (uint8_t digit = 0; digit < 6; ++digit) {
        const char character = value[digit];
        uint8_t nibble;
        if (character >= '0' && character <= '9') nibble = static_cast<uint8_t>(character - '0');
        else if (character >= 'A' && character <= 'F') nibble = static_cast<uint8_t>(character - 'A' + 10);
        else if (character >= 'a' && character <= 'f') nibble = static_cast<uint8_t>(character - 'a' + 10);
        else return false;
        parsed = (parsed << 4) | nibble;
    }
    if (value[6] != '\"') return false;
    *color = parsed;
    return true;
}

bool normalize_clock(const char* source, char* output) {
    uint8_t digits = 0;
    for (; source && *source && digits < CLOCK_DIGITS; ++source)
        if (*source >= '0' && *source <= '9') output[digits++] = *source;
    output[digits] = '\0';
    return digits == CLOCK_DIGITS;
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
    if (!state) return nullptr;
    for (auto& instance : state->instances)
        if (instance.active && instance.instance_id == instance_id) return &instance;
    return nullptr;
}

InstanceState* create_instance(PackageState* state, uint32_t instance_id) {
    if (!state) return nullptr;
    for (auto& instance : state->instances) if (!instance.active) {
        instance = {};
        instance.active = true;
        instance.instance_id = instance_id;
        instance.random_state = instance_id ^ 0x9E3779B9u;
        return &instance;
    }
    return nullptr;
}

uint32_t next_random(InstanceState* instance) {
    instance->random_state = instance->random_state * 1664525u + 1013904223u;
    return instance->random_state;
}

Rect make_rect(int16_t x, int16_t y, uint16_t width, uint16_t height) {
    return {x, y, width, height};
}

bool intersects(const Rect& left, const Rect& right) {
    return left.x < right.x + right.width && left.x + left.width > right.x &&
           left.y < right.y + right.height && left.y + left.height > right.y;
}

void extend_dirty(Rect* dirty, const Rect& candidate, uint16_t width, uint16_t height) {
    if (!candidate.width || !candidate.height) return;
    if (!dirty->width || !dirty->height) { *dirty = candidate; return; }
    int16_t left = dirty->x < candidate.x ? dirty->x : candidate.x;
    int16_t top = dirty->y < candidate.y ? dirty->y : candidate.y;
    int32_t right = dirty->x + dirty->width > candidate.x + candidate.width ?
        dirty->x + dirty->width : candidate.x + candidate.width;
    int32_t bottom = dirty->y + dirty->height > candidate.y + candidate.height ?
        dirty->y + dirty->height : candidate.y + candidate.height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > width) right = width;
    if (bottom > height) bottom = height;
    *dirty = make_rect(left, top, right > left ? right - left : 0, bottom > top ? bottom - top : 0);
}

void add_brick(InstanceState* instance, int16_t x, int16_t y) {
    if (instance->brick_count >= MAX_CLOCK_BRICKS) return;
    Brick& brick = instance->bricks[instance->brick_count++];
    brick.bounds = make_rect(x, y, instance->brick_width, instance->brick_height);
    brick.variant = static_cast<uint8_t>((instance->brick_count - 1u) % BRICK_VARIANTS);
}

void build_clock_bricks(InstanceState* instance) {
    instance->brick_count = 0;
    const uint16_t digit_gap = instance->brick_width;
    const uint16_t content_width = 17u * instance->brick_width;
    int16_t x = static_cast<int16_t>((instance->width - content_width) / 2u);
    const int16_t clock_height = 5 * instance->brick_height;
    const int16_t level_height = LEVEL_ROWS * instance->brick_height;
    const int16_t level_gap = 2 * instance->brick_height;
    const int16_t desired_y = static_cast<int16_t>(instance->height / 3u - clock_height / 2);
    const int16_t maximum_y = static_cast<int16_t>(instance->paddle_y - instance->ball_size -
                                                    clock_height - level_gap - level_height - 6);
    int16_t y = 4;
    if (maximum_y >= 4) {
        y = desired_y < 4 ? 4 : desired_y;
        if (y > maximum_y) y = maximum_y;
    }
    for (uint8_t digit_index = 0; digit_index < CLOCK_DIGITS; ++digit_index) {
        if (digit_index == 2) {
            const int16_t colon_x = x;
            add_brick(instance, colon_x, y + instance->brick_height);
            add_brick(instance, colon_x, y + 3 * instance->brick_height);
            x += static_cast<int16_t>(instance->brick_width + digit_gap);
        }
        const uint8_t digit = static_cast<uint8_t>(instance->clock[digit_index] - '0');
        for (uint8_t row = 0; row < DIGIT_ROWS; ++row)
            for (uint8_t column = 0; column < DIGIT_COLUMNS; ++column)
                if (DIGITS[digit][row] & (1u << (DIGIT_COLUMNS - column - 1)))
                    add_brick(instance, x + column * instance->brick_width,
                              y + row * instance->brick_height);
        x += static_cast<int16_t>(DIGIT_COLUMNS * instance->brick_width + digit_gap);
    }
}

void build_level(InstanceState* instance, int16_t clock_top, int16_t clock_bottom) {
    instance->level_count = 0;
    const int16_t level_height = LEVEL_ROWS * instance->brick_height;
    (void)clock_top;
    const int16_t top = static_cast<int16_t>(clock_bottom + instance->brick_height * 2u);
    const int16_t maximum_top = static_cast<int16_t>(instance->paddle_y - instance->ball_size - level_height - 6);
    if (top > maximum_top) return;
    const int16_t left = static_cast<int16_t>((instance->width - LEVEL_COLUMNS * instance->brick_width) / 2u);
    for (uint8_t row = 0; row < LEVEL_ROWS; ++row) {
        const uint32_t pattern = LEVEL_PATTERNS[instance->level_index % LEVEL_COUNT][row];
        for (uint8_t column = 0; column < LEVEL_COLUMNS; ++column) {
            const uint32_t bit = 1u << (LEVEL_COLUMNS - column - 1u);
            if (!(pattern & bit) || instance->level_count >= MAX_LEVEL_BRICKS) continue;
            LevelBrick& brick = instance->level_bricks[instance->level_count++];
            brick.bounds = make_rect(left + column * instance->brick_width,
                                     top + row * instance->brick_height,
                                     instance->brick_width, instance->brick_height);
            brick.variant = static_cast<uint8_t>((instance->level_index + row + column) % BRICK_VARIANTS);
            brick.present = true;
        }
    }
}

bool level_is_cleared(const InstanceState* instance) {
    for (uint8_t index = 0; index < instance->level_count; ++index)
        if (instance->level_bricks[index].present) return false;
    return instance->level_count != 0;
}

bool line_intersects_rect(float start_x, float start_y, float end_x, float end_y, const Rect& rect, float margin) {
    const float left = rect.x - margin;
    const float right = rect.x + rect.width + margin;
    const float top = rect.y - margin;
    const float bottom = rect.y + rect.height + margin;
    const float delta_x = end_x - start_x;
    const float delta_y = end_y - start_y;
    float entry = 0.0f;
    float exit = 1.0f;
    const float positions[2] = {start_x, start_y};
    const float deltas[2] = {delta_x, delta_y};
    const float minimums[2] = {left, top};
    const float maximums[2] = {right, bottom};
    for (uint8_t axis = 0; axis < 2; ++axis) {
        if (deltas[axis] == 0.0f) {
            if (positions[axis] < minimums[axis] || positions[axis] > maximums[axis]) return false;
            continue;
        }
        float first = (minimums[axis] - positions[axis]) / deltas[axis];
        float second = (maximums[axis] - positions[axis]) / deltas[axis];
        if (first > second) { const float swap = first; first = second; second = swap; }
        if (first > entry) entry = first;
        if (second < exit) exit = second;
        if (entry > exit) return false;
    }
    return exit >= 0.0f && entry <= 1.0f;
}

bool find_ceiling_target(InstanceState* instance, float start_x, float start_y, float* target_x) {
    const float margin = instance->ball_size / 2.0f;
    const float ceiling_y = margin;
    for (uint8_t attempt = 0; attempt < 18; ++attempt) {
        const float candidate = margin + static_cast<float>(next_random(instance) % 1001u) *
            (instance->width - margin * 2.0f) / 1000.0f;
        bool blocked = false;
        for (uint8_t index = 0; index < instance->level_count && !blocked; ++index)
            if (instance->level_bricks[index].present &&
                line_intersects_rect(start_x, start_y, candidate, ceiling_y, instance->level_bricks[index].bounds, margin))
                blocked = true;
        for (uint8_t index = 0; index < instance->brick_count && !blocked; ++index)
            if (instance->bricks[index].respawn_at_ms <= instance->last_step_ms &&
                line_intersects_rect(start_x, start_y, candidate, ceiling_y, instance->bricks[index].bounds, margin))
                blocked = true;
        if (!blocked) { *target_x = candidate; return true; }
    }
    return false;
}

bool find_level_target(InstanceState* instance, float start_x, float start_y, float* target_x, float* target_y) {
    const float margin = instance->ball_size / 2.0f;
    for (int16_t row_y = instance->paddle_y; row_y >= 0; row_y -= instance->brick_height) {
        for (uint8_t attempt = 0; attempt < instance->level_count; ++attempt) {
            const uint8_t index = static_cast<uint8_t>(next_random(instance) % instance->level_count);
            const LevelBrick& candidate = instance->level_bricks[index];
            if (!candidate.present || candidate.bounds.y > row_y ||
                candidate.bounds.y + candidate.bounds.height <= row_y - instance->brick_height) continue;
            const int16_t offset_percent = static_cast<int16_t>(next_random(instance) % 61u) - 30;
            const float candidate_x = candidate.bounds.x + candidate.bounds.width / 2.0f +
                offset_percent * candidate.bounds.width / 100.0f;
            const float candidate_y = candidate.bounds.y + candidate.bounds.height / 2.0f;
            bool blocked = false;
            for (uint8_t brick_index = 0; brick_index < instance->brick_count && !blocked; ++brick_index)
                if (instance->bricks[brick_index].respawn_at_ms <= instance->last_step_ms &&
                    line_intersects_rect(start_x, start_y, candidate_x, candidate_y,
                                         instance->bricks[brick_index].bounds, margin))
                    blocked = true;
            for (uint8_t brick_index = 0; brick_index < instance->level_count && !blocked; ++brick_index) {
                const LevelBrick& blocker = instance->level_bricks[brick_index];
                if (brick_index != index && blocker.present &&
                    line_intersects_rect(start_x, start_y, candidate_x, candidate_y, blocker.bounds, margin))
                    blocked = true;
            }
            if (!blocked) {
                *target_x = candidate_x;
                *target_y = candidate_y;
                return true;
            }
        }
    }
    return false;
}

void reset_ball(InstanceState* instance) {
    instance->ball_x = instance->width / 2.0f - instance->ball_size / 2.0f;
    instance->ball_y = instance->paddle_y - instance->ball_size - 3.0f;
    const float scale = static_cast<float>(instance->height) * 0.40f;
    instance->ball_dx = scale * 0.72f;
    instance->ball_dy = -scale;
}

void respawn_clock_bricks(InstanceState* instance, uint32_t now) {
    for (uint8_t index = 0; index < instance->brick_count; ++index)
        if (instance->bricks[index].respawn_at_ms > now) instance->bricks[index].respawn_at_ms = 0;
}

void step_game(InstanceState* instance, uint32_t now) {
    uint32_t elapsed_ms = instance->last_step_ms ? now - instance->last_step_ms : 33;
    if (elapsed_ms > 100) elapsed_ms = 100;
    instance->last_step_ms = now;
    const float seconds = static_cast<float>(elapsed_ms) / 1000.0f;
    const float speed = static_cast<float>(instance->speed_percent) / 100.0f;
    const float ball_center = instance->ball_x + instance->ball_size / 2.0f;
    const float paddle_center = instance->paddle_x + instance->paddle_width / 2.0f;
    const float tracking = static_cast<float>(instance->paddle_accuracy) / 100.0f;
    instance->paddle_x += (ball_center - paddle_center) * tracking;
    if (instance->paddle_x < 0) instance->paddle_x = 0;
    const float max_paddle_x = instance->width > instance->paddle_width ? instance->width - instance->paddle_width : 0;
    if (instance->paddle_x > max_paddle_x) instance->paddle_x = max_paddle_x;

    instance->ball_x += instance->ball_dx * seconds * speed;
    instance->ball_y += instance->ball_dy * seconds * speed;
    const float max_ball_x = instance->width > instance->ball_size ? instance->width - instance->ball_size : 0;
    if (instance->ball_x < 0 || instance->ball_x > max_ball_x) {
        if (instance->ball_x < 0) instance->ball_x = 0;
        if (instance->ball_x > max_ball_x) instance->ball_x = max_ball_x;
        instance->ball_dx = -instance->ball_dx;
    }
    if (instance->ball_y < 0) { instance->ball_y = 0; instance->ball_dy = -instance->ball_dy; }

    const Rect ball = make_rect(static_cast<int16_t>(instance->ball_x), static_cast<int16_t>(instance->ball_y),
                                instance->ball_size, instance->ball_size);
    for (uint8_t index = 0; index < instance->level_count; ++index) {
        LevelBrick& brick = instance->level_bricks[index];
        if (!brick.present || !intersects(ball, brick.bounds)) continue;
        brick.present = false;
        instance->ball_dy = -instance->ball_dy;
        if (instance->ball_dy > 0) instance->ball_y = brick.bounds.y + brick.bounds.height;
        else instance->ball_y = brick.bounds.y - instance->ball_size;
        if (level_is_cleared(instance)) {
            const uint8_t previous_level = instance->level_index;
            instance->level_index = static_cast<uint8_t>(next_random(instance) % LEVEL_COUNT);
            if (instance->level_index == previous_level)
                instance->level_index = static_cast<uint8_t>((instance->level_index + 1u) % LEVEL_COUNT);
            const int16_t clock_top = instance->bricks[0].bounds.y;
            const int16_t clock_bottom = static_cast<int16_t>(clock_top + 5 * instance->brick_height);
            build_level(instance, clock_top, clock_bottom);
            instance->full_render = true;
        }
        return;
    }
    for (uint8_t index = 0; index < instance->brick_count; ++index) {
        Brick& brick = instance->bricks[index];
        if (brick.respawn_at_ms > now || !intersects(ball, brick.bounds)) continue;
        brick.respawn_at_ms = now + (instance->respawn_on_paddle ? PADDLE_RESPAWN_SAFETY_MS : instance->respawn_ms);
        instance->ball_dy = -instance->ball_dy;
        if (instance->ball_dy > 0) instance->ball_y = brick.bounds.y + brick.bounds.height;
        else instance->ball_y = brick.bounds.y - instance->ball_size;
        break;
    }

    if (instance->ball_dy > 0 && instance->ball_y + instance->ball_size >= instance->paddle_y &&
        instance->ball_x + instance->ball_size >= instance->paddle_x &&
        instance->ball_x <= instance->paddle_x + instance->paddle_width) {
        const float impact = (instance->ball_x + instance->ball_size / 2.0f - paddle_center) /
            (instance->paddle_width / 2.0f);
        const float scale = static_cast<float>(instance->height) * 0.40f;
        instance->ball_dy = -scale;
        float target_x = 0.0f;
        float target_y = 0.0f;
        if (find_level_target(instance, ball_center, instance->paddle_y, &target_x, &target_y)) {
            const float travel = instance->paddle_y > target_y ? instance->paddle_y - target_y : 1.0f;
            const int16_t miss_percent = static_cast<int16_t>(next_random(instance) % 121u) - 60;
            const float miss = miss_percent * instance->brick_width / 100.0f;
            instance->ball_dx = (target_x + miss - ball_center) * scale / travel;
        } else if (next_random(instance) % CEILING_SHOT_DENOMINATOR == 0 &&
                   find_ceiling_target(instance, ball_center, instance->paddle_y, &target_x)) {
            const float travel = instance->paddle_y > instance->ball_size ?
                static_cast<float>(instance->paddle_y - instance->ball_size) : 1.0f;
            instance->ball_dx = (target_x - ball_center) * scale / travel;
        } else {
            instance->ball_dx = impact * scale * 0.85f;
            if (instance->ball_dx > -scale * 0.18f && instance->ball_dx < scale * 0.18f)
                instance->ball_dx = impact < 0 ? -scale * 0.18f : scale * 0.18f;
        }
        instance->ball_y = instance->paddle_y - instance->ball_size - 1.0f;
        if (instance->respawn_on_paddle) respawn_clock_bricks(instance, now);
    }
    if (instance->ball_y > instance->height) reset_ball(instance);
}

uint32_t rgb565_to_rgb(uint16_t color) {
    const uint32_t red = static_cast<uint32_t>((color >> 11) & 0x1Fu) * 255u / 31u;
    const uint32_t green = static_cast<uint32_t>((color >> 5) & 0x3Fu) * 255u / 63u;
    const uint32_t blue = static_cast<uint32_t>(color & 0x1Fu) * 255u / 31u;
    return (red << 16) | (green << 8) | blue;
}

void draw_masked_sprite(const NativeExtensionHostApi* host, InstanceState* instance, const Rect& destination,
                        const uint16_t* pixels, uint16_t source_width, uint16_t source_height) {
    for (uint16_t source_y = 0; source_y < source_height; ++source_y) {
        const int16_t top = static_cast<int16_t>(destination.y + source_y * destination.height / source_height);
        const int16_t bottom = static_cast<int16_t>(destination.y + (source_y + 1u) * destination.height / source_height);
        if (bottom <= top) continue;
        for (uint16_t source_x = 0; source_x < source_width;) {
            const uint16_t color = pixels[source_y * source_width + source_x];
            uint16_t end = source_x + 1u;
            while (end < source_width && pixels[source_y * source_width + end] == color) ++end;
            const int16_t left = static_cast<int16_t>(destination.x + source_x * destination.width / source_width);
            const int16_t right = static_cast<int16_t>(destination.x + end * destination.width / source_width);
            if (color && right > left)
                host->canvas->canvas_fill_rect(instance->canvas, left, top, right - left, bottom - top, rgb565_to_rgb(color));
            source_x = end;
        }
    }
}

void draw_brick(const NativeExtensionHostApi* host, InstanceState* instance, const Brick& brick) {
    const Rect& bounds = brick.bounds;
    if (instance->brick_color_custom) {
        host->canvas->canvas_fill_rect(instance->canvas, bounds.x, bounds.y, bounds.width, bounds.height, instance->brick_color);
        return;
    }
    host->canvas->canvas_blit_rgb565(instance->canvas, bounds.x, bounds.y,
                                     BRICK_SPRITES[brick.variant], BRICK_WIDTH, BRICK_HEIGHT,
                                     bounds.width, bounds.height);
}

void draw_level_brick(const NativeExtensionHostApi* host, InstanceState* instance, const LevelBrick& brick) {
    host->canvas->canvas_blit_rgb565(instance->canvas, brick.bounds.x, brick.bounds.y,
                                     BRICK_SPRITES[brick.variant], BRICK_WIDTH, BRICK_HEIGHT,
                                     brick.bounds.width, brick.bounds.height);
}

void draw_paddle(const NativeExtensionHostApi* host, InstanceState* instance, const Rect& paddle) {
    draw_masked_sprite(host, instance, paddle, PAD, PAD_WIDTH, PAD_HEIGHT);
}

void draw_ball(const NativeExtensionHostApi* host, InstanceState* instance, const Rect& ball) {
    draw_masked_sprite(host, instance, ball, BALL, BALL_WIDTH, BALL_HEIGHT);
}

void redraw_bricks_in_rect(const NativeExtensionHostApi* host, InstanceState* instance, const Rect& dirty, uint32_t now) {
    for (uint8_t index = 0; index < instance->brick_count; ++index) {
        Brick& brick = instance->bricks[index];
        const bool visible = brick.respawn_at_ms <= now;
        if (visible && intersects(dirty, brick.bounds)) draw_brick(host, instance, brick);
        brick.rendered = visible;
    }
}

void redraw_level_in_rect(const NativeExtensionHostApi* host, InstanceState* instance, const Rect& dirty) {
    for (uint8_t index = 0; index < instance->level_count; ++index) {
        LevelBrick& brick = instance->level_bricks[index];
        if (brick.present && intersects(dirty, brick.bounds)) draw_level_brick(host, instance, brick);
        brick.rendered = brick.present;
    }
}

void render(const NativeExtensionHostApi* host, InstanceState* instance, uint32_t now) {
    const Rect ball = make_rect(static_cast<int16_t>(instance->ball_x), static_cast<int16_t>(instance->ball_y),
                                instance->ball_size, instance->ball_size);
    const Rect paddle = make_rect(static_cast<int16_t>(instance->paddle_x), instance->paddle_y,
                                  instance->paddle_width, instance->paddle_height);
    if (instance->full_render) {
        host->canvas->canvas_clear(instance->canvas, instance->background_color);
        const Rect full = make_rect(0, 0, instance->width, instance->height);
        redraw_level_in_rect(host, instance, full);
        redraw_bricks_in_rect(host, instance, full, now);
        draw_paddle(host, instance, paddle);
        draw_ball(host, instance, ball);
        host->canvas->canvas_invalidate_rect(instance->canvas, 0, 0, instance->width, instance->height);
        instance->full_render = false;
    } else {
        Rect dirty = {};
        extend_dirty(&dirty, instance->rendered_ball, instance->width, instance->height);
        extend_dirty(&dirty, instance->rendered_paddle, instance->width, instance->height);
        extend_dirty(&dirty, ball, instance->width, instance->height);
        extend_dirty(&dirty, paddle, instance->width, instance->height);
        for (uint8_t index = 0; index < instance->brick_count; ++index) {
            Brick& brick = instance->bricks[index];
            const bool visible = brick.respawn_at_ms <= now;
            if (visible != brick.rendered) extend_dirty(&dirty, brick.bounds, instance->width, instance->height);
        }
        for (uint8_t index = 0; index < instance->level_count; ++index) {
            LevelBrick& brick = instance->level_bricks[index];
            if (brick.present != brick.rendered) extend_dirty(&dirty, brick.bounds, instance->width, instance->height);
        }
        if (dirty.width && dirty.height) {
            host->canvas->canvas_fill_rect(instance->canvas, dirty.x, dirty.y, dirty.width, dirty.height, instance->background_color);
            redraw_level_in_rect(host, instance, dirty);
            redraw_bricks_in_rect(host, instance, dirty, now);
            draw_paddle(host, instance, paddle);
            draw_ball(host, instance, ball);
            host->canvas->canvas_invalidate_rect(instance->canvas, dirty.x, dirty.y, dirty.width, dirty.height);
        }
    }
    instance->rendered_ball = ball;
    instance->rendered_paddle = paddle;
    instance->render_dirty = false;
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
    if (instance->width < 40 || instance->height < 40) { instance->active = false; return; }
    copy_text(instance->time_template, sizeof(instance->time_template), "[time:%H%M]");
    parse_config_string(config_json, "time", instance->time_template, sizeof(instance->time_template));
    instance->respawn_ms = parse_config_uint(config_json, "respawn_ms", DEFAULT_RESPAWN_MS, 10000);
    instance->respawn_on_paddle = parse_config_bool(config_json, "respawn_on_paddle", true);
    instance->speed_percent = parse_config_uint(config_json, "speed", DEFAULT_SPEED_PERCENT, 400);
    if (instance->speed_percent < 25) instance->speed_percent = 25;
    instance->paddle_accuracy = parse_config_uint(config_json, "paddle_accuracy", DEFAULT_PADDLE_ACCURACY, 100);
    instance->background_color = DEFAULT_BACKGROUND;
    instance->brick_color = DEFAULT_BRICK_COLOR;
    NativeExtensionButtonSnapshot button = {};
    if (host->button && host->button->get(extension_context, instance_id, &button))
        instance->background_color = button.background_rgb;
    instance->brick_color_custom = parse_hex_color(find_config_value(config_json, "brick_color"), &instance->brick_color);
    char resolved[16] = {};
    if (host->binding->resolve(extension_context, instance_id, instance->time_template, resolved, sizeof(resolved)))
        normalize_clock(resolved, instance->clock);
    if (!instance->clock[0]) copy_text(instance->clock, sizeof(instance->clock), "0000");
    instance->brick_width = instance->width / 18u;
    if (instance->brick_width < 5) instance->brick_width = 5;
    instance->brick_height = instance->brick_width / 2u;
    if (instance->brick_height < 3) instance->brick_height = 3;
    instance->ball_size = instance->brick_height > 6 ? instance->brick_height : 6;
    instance->paddle_height = static_cast<uint16_t>(instance->ball_size * PAD_HEIGHT / BALL_HEIGHT);
    instance->paddle_width = static_cast<uint16_t>(instance->paddle_height * PAD_WIDTH / PAD_HEIGHT);
    if (instance->paddle_width < 18) instance->paddle_width = 18;
    if (instance->paddle_width > instance->width) instance->paddle_width = instance->width;
    instance->paddle_y = static_cast<int16_t>(instance->height - instance->paddle_height - 6);
    instance->paddle_x = (instance->width - instance->paddle_width) / 2.0f;
    instance->canvas = host->canvas->canvas_create(root);
    instance->canvas_buffer = host->core->alloc(host->canvas->canvas_buffer_size(instance->width, instance->height));
    if (!instance->canvas || !instance->canvas_buffer) { instance->active = false; return; }
    host->canvas->canvas_set_buffer(instance->canvas, instance->canvas_buffer, instance->width, instance->height);
    build_clock_bricks(instance);
    const int16_t clock_top = instance->bricks[0].bounds.y;
    const int16_t clock_bottom = static_cast<int16_t>(clock_top + 5 * instance->brick_height);
    build_level(instance, clock_top, clock_bottom);
    reset_ball(instance);
    instance->full_render = true;
    instance->render_dirty = true;
}

extern "C" void native_extension_destroy_instance(const NativeExtensionHostApi* host, void* extension_context,
                                                   uint32_t instance_id) {
    if (!host || !host->core) return;
    PackageState* state = static_cast<PackageState*>(host->core->context_get_data(extension_context));
    InstanceState* instance = find_instance(state, instance_id);
    if (!instance) return;
    if (instance->canvas_buffer) host->core->free(instance->canvas_buffer);
    *instance = {};
}

extern "C" void native_extension_shutdown(const NativeExtensionHostApi* host, void* extension_context) {
    if (!host || !host->core) return;
    PackageState* state = static_cast<PackageState*>(host->core->context_get_data(extension_context));
    if (state) host->core->free(state);
    host->core->context_set_data(extension_context, nullptr);
}

extern "C" void native_extension_tick(const NativeExtensionHostApi* host, void* extension_context, uint32_t instance_id) {
    if (!host || !host->core || !host->binding || !host->canvas) return;
    PackageState* state = static_cast<PackageState*>(host->core->context_get_data(extension_context));
    InstanceState* instance = find_instance(state, instance_id);
    if (!instance || !instance->canvas) return;
    const uint32_t now = host->core->millis();
    if (now >= instance->next_clock_resolve_ms) {
        char resolved[16] = {};
        char clock[5] = {};
        if (host->binding->resolve(extension_context, instance_id, instance->time_template, resolved, sizeof(resolved)) &&
            normalize_clock(resolved, clock) && !text_equals(instance->clock, clock)) {
            copy_text(instance->clock, sizeof(instance->clock), clock);
            build_clock_bricks(instance);
            instance->full_render = true;
        }
        instance->next_clock_resolve_ms = now + CLOCK_RESOLVE_MS;
    }
    step_game(instance, now);
    render(host, instance, now);
}

extern "C" NativeExtensionEventResult native_extension_on_tap(const NativeExtensionHostApi*, void*, uint32_t) {
    return NATIVE_EXTENSION_PASS_THROUGH;
}

extern "C" NativeExtensionEventResult native_extension_on_long_press(const NativeExtensionHostApi*, void*, uint32_t) {
    return NATIVE_EXTENSION_PASS_THROUGH;
}
