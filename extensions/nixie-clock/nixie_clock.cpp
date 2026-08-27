#include "native_extension_api.h"

extern "C" const NativeExtensionDescriptor native_extension_descriptor = {
    NATIVE_EXTENSION_DESCRIPTOR_MAGIC, NATIVE_EXTENSION_ABI_VERSION,
    NATIVE_EXTENSION_TARGET_ABI, "nixie-clock", "1.0.0", "Nixie Clock",
    50, 0,
};

namespace {

// The device supports up to sixteen pad buttons, so do not silently leave a
// valid Extension placement blank when the same package is used on many pads.
constexpr uint8_t MAX_INSTANCES = 16;
constexpr uint8_t MAX_CLOCK_DIGITS = 6;
constexpr uint8_t MAX_CLOCK_TOKENS = 8;
constexpr uint8_t CLOCK_TEXT_CAPACITY = MAX_CLOCK_DIGITS + 1;
constexpr uint8_t TIME_TEMPLATE_CAPACITY = 96;
constexpr uint16_t LOGICAL_GAP = 6;
constexpr uint32_t CLOCK_RESOLVE_MS = 250;
constexpr uint32_t BLACK = 0x000000;
constexpr uint16_t DEFAULT_TRANSITION_INTENSITY = 15;
constexpr uint16_t DEFAULT_TRANSITION_MS = 180;
constexpr uint16_t DEFAULT_SEPARATOR_INTENSITY = 70;
constexpr uint16_t DEFAULT_SEPARATOR_PERIOD_MS = 1000;
constexpr uint16_t DEFAULT_FLICKER_INTENSITY = 10;
constexpr uint16_t DEFAULT_FLICKER_DURATION_MS = 80;
constexpr uint16_t DEFAULT_FLICKER_MIN_INTERVAL_S = 15;
constexpr uint16_t DEFAULT_FLICKER_MAX_INTERVAL_S = 90;

#include "nixie_assets.inc"

struct ClockLayout {
    uint16_t digit_width;
    uint16_t digit_height;
    uint16_t gap;
    int16_t positions[MAX_CLOCK_TOKENS];
    int16_t top;
    uint8_t token_count;
};

struct InstanceState {
    uint8_t active;
    uint8_t render_dirty;
    uint8_t rendered_digit_count;
    uint8_t flicker_token;
    uint32_t instance_id;
    void* extension_context;
    void* canvas;
    void* canvas_buffer;
    uint16_t* sprite_buffer;
    uint16_t width;
    uint16_t height;
    char time_template[TIME_TEMPLATE_CAPACITY];
    char clock[CLOCK_TEXT_CAPACITY];
    char rendered_clock[CLOCK_TEXT_CAPACITY];
    uint32_t next_clock_resolve_ms;
    uint32_t transition_started_ms[MAX_CLOCK_DIGITS];
    uint32_t next_flicker_ms;
    uint32_t flicker_ends_ms;
    uint32_t random_state;
    uint16_t transition_intensity;
    uint16_t transition_ms;
    uint16_t separator_intensity;
    uint16_t separator_period_ms;
    uint16_t flicker_intensity;
    uint16_t flicker_duration_ms;
    uint16_t flicker_min_interval_s;
    uint16_t flicker_max_interval_s;
    uint16_t rendered_separator_brightness;
    ClockLayout layout;
};

struct PackageState {
    InstanceState instances[MAX_INSTANCES];
};

void copy_text(char* out, size_t capacity, const char* value) {
    if (!out || capacity == 0) return;
    size_t index = 0;
    while (value && value[index] && index + 1 < capacity) {
        out[index] = value[index];
        ++index;
    }
    out[index] = '\0';
}

bool text_equals(const char* left, const char* right) {
    size_t index = 0;
    while (left[index] && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

bool parse_config_string(const char* json, const char* key, char* out, size_t capacity) {
    if (!json || !key || !out || capacity == 0) return false;
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
        if (*value++ != '\"') return false;
        index = 0;
        while (*value && *value != '\"' && index + 1 < capacity) out[index++] = *value++;
        out[index] = '\0';
        return out[0] != '\0';
    }
    return false;
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

uint32_t next_random(InstanceState* instance) {
    instance->random_state = instance->random_state * 1664525u + 1013904223u;
    return instance->random_state;
}

uint8_t normalize_clock(const char* source, char* out) {
    uint8_t count = 0;
    for (; source && *source && count < MAX_CLOCK_DIGITS; ++source) {
        if (*source >= '0' && *source <= '9') out[count++] = *source;
    }
    out[count] = '\0';
    // A Nixie clock supports exactly HHMM or HHMMSS. Reject partial/error text.
    return count == 4 || count == 6 ? count : 0;
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
    for (auto& instance : state->instances) {
        if (instance.active) continue;
        instance = {};
        instance.active = true;
        instance.instance_id = instance_id;
        return &instance;
    }
    return nullptr;
}

const uint16_t* sprite_palette(uint8_t sprite) {
    switch (sprite) {
        case 0: return NIXIE_PALETTE_0;
        case 1: return NIXIE_PALETTE_1;
        case 2: return NIXIE_PALETTE_2;
        case 3: return NIXIE_PALETTE_3;
        case 4: return NIXIE_PALETTE_4;
        case 5: return NIXIE_PALETTE_5;
        case 6: return NIXIE_PALETTE_6;
        case 7: return NIXIE_PALETTE_7;
        case 8: return NIXIE_PALETTE_8;
        case 9: return NIXIE_PALETTE_9;
        default: return NIXIE_PALETTE_10;
    }
}

uint8_t sprite_palette_size(uint8_t sprite) {
    switch (sprite) {
        case 0: return NIXIE_PALETTE_0_SIZE;
        case 1: return NIXIE_PALETTE_1_SIZE;
        case 2: return NIXIE_PALETTE_2_SIZE;
        case 3: return NIXIE_PALETTE_3_SIZE;
        case 4: return NIXIE_PALETTE_4_SIZE;
        case 5: return NIXIE_PALETTE_5_SIZE;
        case 6: return NIXIE_PALETTE_6_SIZE;
        case 7: return NIXIE_PALETTE_7_SIZE;
        case 8: return NIXIE_PALETTE_8_SIZE;
        case 9: return NIXIE_PALETTE_9_SIZE;
        default: return NIXIE_PALETTE_10_SIZE;
    }
}

const uint8_t* sprite_rle(uint8_t sprite, uint16_t* size) {
    switch (sprite) {
        case 0: *size = sizeof(NIXIE_RLE_0); return NIXIE_RLE_0;
        case 1: *size = sizeof(NIXIE_RLE_1); return NIXIE_RLE_1;
        case 2: *size = sizeof(NIXIE_RLE_2); return NIXIE_RLE_2;
        case 3: *size = sizeof(NIXIE_RLE_3); return NIXIE_RLE_3;
        case 4: *size = sizeof(NIXIE_RLE_4); return NIXIE_RLE_4;
        case 5: *size = sizeof(NIXIE_RLE_5); return NIXIE_RLE_5;
        case 6: *size = sizeof(NIXIE_RLE_6); return NIXIE_RLE_6;
        case 7: *size = sizeof(NIXIE_RLE_7); return NIXIE_RLE_7;
        case 8: *size = sizeof(NIXIE_RLE_8); return NIXIE_RLE_8;
        case 9: *size = sizeof(NIXIE_RLE_9); return NIXIE_RLE_9;
        default: *size = sizeof(NIXIE_RLE_10); return NIXIE_RLE_10;
    }
}

bool decode_sprite(InstanceState* instance, uint8_t sprite) {
    uint16_t rle_size = 0;
    const uint16_t* palette = sprite_palette(sprite);
    const uint8_t palette_size = sprite_palette_size(sprite);
    const uint8_t* rle = sprite_rle(sprite, &rle_size);
    const uint32_t expected = static_cast<uint32_t>(NIXIE_SPRITE_WIDTH) * NIXIE_SPRITE_HEIGHT;
    uint32_t destination = 0;
    for (uint16_t offset = 0; offset + 1 < rle_size && destination < expected; offset += 2) {
        const uint8_t count = rle[offset];
        const uint8_t palette_index = rle[offset + 1];
        if (count == 0 || palette_index >= palette_size) return false;
        for (uint8_t index = 0; index < count && destination < expected; ++index)
            instance->sprite_buffer[destination++] = palette[palette_index];
    }
    return destination == expected;
}

void apply_brightness(InstanceState* instance, uint16_t percent) {
    if (percent == 100) return;
    const uint32_t pixel_count = static_cast<uint32_t>(NIXIE_SPRITE_WIDTH) * NIXIE_SPRITE_HEIGHT;
    for (uint32_t index = 0; index < pixel_count; ++index) {
        const uint16_t value = instance->sprite_buffer[index];
        const uint16_t red = static_cast<uint16_t>(((value >> 11) & 0x1Fu) * percent / 100u);
        const uint16_t green = static_cast<uint16_t>(((value >> 5) & 0x3Fu) * percent / 100u);
        const uint16_t blue = static_cast<uint16_t>((value & 0x1Fu) * percent / 100u);
        instance->sprite_buffer[index] = static_cast<uint16_t>((red > 31u ? 31u : red) << 11 |
                                                               (green > 63u ? 63u : green) << 5 |
                                                               (blue > 31u ? 31u : blue));
    }
}

uint8_t token_count_for_digits(uint8_t digit_count) {
    return digit_count == 6 ? 8 : 5;
}

bool token_is_separator(uint8_t digit_count, uint8_t token) {
    return token == 2 || (digit_count == 6 && token == 5);
}

uint8_t token_digit_index(uint8_t digit_count, uint8_t token) {
    if (token < 2) return token;
    if (digit_count == 4) return static_cast<uint8_t>(token - 1);
    return token < 5 ? static_cast<uint8_t>(token - 1) : static_cast<uint8_t>(token - 2);
}

bool build_layout(InstanceState* instance, uint8_t digit_count) {
    const uint8_t tokens = token_count_for_digits(digit_count);
    const uint32_t logical_width = static_cast<uint32_t>(tokens) * NIXIE_SPRITE_WIDTH +
                                   static_cast<uint32_t>(tokens - 1) * LOGICAL_GAP;
    if (instance->width == 0 || instance->height == 0 || logical_width == 0) return false;
    float scale_x = static_cast<float>(instance->width) / static_cast<float>(logical_width);
    float scale_y = static_cast<float>(instance->height) / static_cast<float>(NIXIE_SPRITE_HEIGHT);
    float scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale > 1.0f) scale = 1.0f;
    const uint16_t sprite_width = static_cast<uint16_t>(NIXIE_SPRITE_WIDTH * scale);
    const uint16_t sprite_height = static_cast<uint16_t>(NIXIE_SPRITE_HEIGHT * scale);
    const uint16_t gap = static_cast<uint16_t>(LOGICAL_GAP * scale);
    if (sprite_width < 4 || sprite_height < 4) return false;
    const uint16_t rendered_width = static_cast<uint16_t>(tokens * sprite_width + (tokens - 1) * gap);
    int16_t position = static_cast<int16_t>((instance->width - rendered_width) / 2u);
    instance->layout = {};
    instance->layout.digit_width = sprite_width;
    instance->layout.digit_height = sprite_height;
    instance->layout.gap = gap;
    instance->layout.top = static_cast<int16_t>((instance->height - sprite_height) / 2u);
    instance->layout.token_count = tokens;
    for (uint8_t token = 0; token < tokens; ++token) {
        instance->layout.positions[token] = position;
        position = static_cast<int16_t>(position + sprite_width + gap);
    }
    return true;
}

uint16_t separator_brightness(const InstanceState* instance, uint8_t token, uint32_t now) {
    if (!instance->separator_intensity || instance->separator_period_ms < 2u) return 100;
    const uint32_t phase = now % instance->separator_period_ms;
    const uint32_t half = instance->separator_period_ms / 2u;
    const uint32_t triangle = phase <= half ? phase * 100u / half : (instance->separator_period_ms - phase) * 100u / half;
    uint16_t brightness = static_cast<uint16_t>(100u - instance->separator_intensity * triangle / 100u);
    if (instance->flicker_ends_ms > now && instance->flicker_token == token)
        brightness = static_cast<uint16_t>(brightness > instance->flicker_intensity ? brightness - instance->flicker_intensity : 1);
    return brightness;
}

uint16_t digit_brightness(const InstanceState* instance, uint8_t token, uint8_t digit_index, uint32_t now) {
    uint16_t brightness = 100;
    const uint32_t started = instance->transition_started_ms[digit_index];
    if (started && instance->transition_ms && now - started < instance->transition_ms) {
        const uint32_t remaining = instance->transition_ms - (now - started);
        brightness = static_cast<uint16_t>(100u + instance->transition_intensity * remaining / instance->transition_ms);
    }
    if (instance->flicker_ends_ms > now && instance->flicker_token == token)
        brightness = static_cast<uint16_t>(brightness > instance->flicker_intensity ? brightness - instance->flicker_intensity : 1);
    return brightness;
}

void draw_token(const NativeExtensionHostApi* host, InstanceState* instance, uint8_t digit_count,
                uint8_t token, uint32_t now) {
    const bool separator = token_is_separator(digit_count, token);
    const int16_t x = instance->layout.positions[token];
    const uint8_t digit_index = token_digit_index(digit_count, token);
    const uint8_t sprite = separator ? 10 : static_cast<uint8_t>(instance->clock[digit_index] - '0');
    if (!decode_sprite(instance, sprite)) return;
    apply_brightness(instance, separator ? separator_brightness(instance, token, now) : digit_brightness(instance, token, digit_index, now));
    host->canvas->canvas_blit_rgb565(instance->canvas, x, instance->layout.top, instance->sprite_buffer,
                                     NIXIE_SPRITE_WIDTH, NIXIE_SPRITE_HEIGHT,
                                     instance->layout.digit_width, instance->layout.digit_height);
}

void draw_clock(const NativeExtensionHostApi* host, InstanceState* instance, bool full_clear, uint32_t now) {
    const uint8_t digit_count = static_cast<uint8_t>(instance->clock[0] ?
        (instance->clock[4] ? 6 : 4) : 0);
    if (!digit_count || !build_layout(instance, digit_count)) return;
    if (full_clear) host->canvas->canvas_clear(instance->canvas, BLACK);
    for (uint8_t token = 0; token < instance->layout.token_count; ++token)
        draw_token(host, instance, digit_count, token, now);
    host->canvas->canvas_invalidate_rect(instance->canvas, 0, 0, instance->width, instance->height);
    copy_text(instance->rendered_clock, sizeof(instance->rendered_clock), instance->clock);
    instance->rendered_digit_count = digit_count;
    instance->rendered_separator_brightness = separator_brightness(instance, 2, now);
    instance->render_dirty = false;
}

void redraw_token(const NativeExtensionHostApi* host, InstanceState* instance, uint8_t token, uint32_t now) {
    if (!instance->rendered_digit_count || token >= instance->layout.token_count) return;
    const int16_t x = instance->layout.positions[token];
    host->canvas->canvas_fill_rect(instance->canvas, x, instance->layout.top,
                                   instance->layout.digit_width, instance->layout.digit_height, BLACK);
    draw_token(host, instance, instance->rendered_digit_count, token, now);
    host->canvas->canvas_invalidate_rect(instance->canvas, x, instance->layout.top,
                                         instance->layout.digit_width, instance->layout.digit_height);
}

void redraw_digit(const NativeExtensionHostApi* host, InstanceState* instance, uint8_t digit_index, uint32_t now) {
    for (uint8_t token = 0; token < instance->layout.token_count; ++token) {
        if (token_is_separator(instance->rendered_digit_count, token)) continue;
        if (token_digit_index(instance->rendered_digit_count, token) == digit_index) {
            redraw_token(host, instance, token, now);
            return;
        }
    }
}

void schedule_flicker(InstanceState* instance, uint32_t now) {
    if (!instance->flicker_intensity || !instance->flicker_duration_ms ||
        !instance->flicker_max_interval_s) {
        instance->next_flicker_ms = 0;
        return;
    }
    const uint16_t minimum = instance->flicker_min_interval_s;
    const uint16_t maximum = instance->flicker_max_interval_s < minimum ? minimum : instance->flicker_max_interval_s;
    const uint32_t span = static_cast<uint32_t>(maximum - minimum + 1u);
    instance->next_flicker_ms = now + (minimum + next_random(instance) % span) * 1000u;
}

}  // namespace

extern "C" void native_extension_create_instance(const NativeExtensionHostApi* host, void* extension_context,
                                                  uint32_t instance_id, void* root, const char* config_json) {
    if (!host || !host->core || !host->ui || !host->canvas || !host->binding || !root) return;
    PackageState* state = package_state(host, extension_context);
    InstanceState* instance = create_instance(state, instance_id);
    if (!instance) return;
    instance->extension_context = extension_context;
    instance->width = static_cast<uint16_t>(host->ui->obj_get_width(root));
    instance->height = static_cast<uint16_t>(host->ui->obj_get_height(root));
    copy_text(instance->time_template, sizeof(instance->time_template), "[time:%H%M]");
    parse_config_string(config_json, "time", instance->time_template, sizeof(instance->time_template));
    const bool animations = parse_config_bool(config_json, "animations", true);
    instance->transition_intensity = animations ? parse_config_uint(config_json, "transition_intensity", DEFAULT_TRANSITION_INTENSITY, 80) : 0;
    instance->transition_ms = animations ? parse_config_uint(config_json, "transition_ms", DEFAULT_TRANSITION_MS, 2000) : 0;
    instance->separator_intensity = animations ? parse_config_uint(config_json, "separator_intensity", DEFAULT_SEPARATOR_INTENSITY, 100) : 0;
    instance->separator_period_ms = animations ? parse_config_uint(config_json, "separator_period_ms", DEFAULT_SEPARATOR_PERIOD_MS, 10000) : 0;
    instance->flicker_intensity = animations ? parse_config_uint(config_json, "flicker_intensity", DEFAULT_FLICKER_INTENSITY, 80) : 0;
    instance->flicker_duration_ms = animations ? parse_config_uint(config_json, "flicker_duration_ms", DEFAULT_FLICKER_DURATION_MS, 1000) : 0;
    instance->flicker_min_interval_s = animations ? parse_config_uint(config_json, "flicker_min_interval_s", DEFAULT_FLICKER_MIN_INTERVAL_S, 3600) : 0;
    instance->flicker_max_interval_s = animations ? parse_config_uint(config_json, "flicker_max_interval_s", DEFAULT_FLICKER_MAX_INTERVAL_S, 3600) : 0;
    instance->random_state = instance_id ^ host->core->millis() ^ 0x9E3779B9u;
    instance->canvas = host->canvas->canvas_create(root);
    instance->canvas_buffer = host->core->alloc(host->canvas->canvas_buffer_size(instance->width, instance->height));
    instance->sprite_buffer = static_cast<uint16_t*>(host->core->alloc(
        static_cast<size_t>(NIXIE_SPRITE_WIDTH) * NIXIE_SPRITE_HEIGHT * sizeof(uint16_t)));
    if (!instance->canvas || !instance->canvas_buffer || !instance->sprite_buffer) return;
    host->canvas->canvas_set_buffer(instance->canvas, instance->canvas_buffer, instance->width, instance->height);
    char resolved[TIME_TEMPLATE_CAPACITY] = {};
    if (host->binding->resolve(extension_context, instance_id, instance->time_template, resolved, sizeof(resolved)))
        normalize_clock(resolved, instance->clock);
    if (!instance->clock[0]) copy_text(instance->clock, sizeof(instance->clock), "0000");
    schedule_flicker(instance, host->core->millis());
    instance->render_dirty = true;
}

extern "C" void native_extension_destroy_instance(const NativeExtensionHostApi* host, void* extension_context,
                                                   uint32_t instance_id) {
    if (!host || !host->core) return;
    PackageState* state = static_cast<PackageState*>(host->core->context_get_data(extension_context));
    InstanceState* instance = find_instance(state, instance_id);
    if (!instance) return;
    if (instance->sprite_buffer) host->core->free(instance->sprite_buffer);
    if (instance->canvas_buffer) host->core->free(instance->canvas_buffer);
    *instance = {};
}

extern "C" void native_extension_shutdown(const NativeExtensionHostApi* host, void* extension_context) {
    if (!host || !host->core) return;
    PackageState* state = static_cast<PackageState*>(host->core->context_get_data(extension_context));
    if (state) host->core->free(state);
    host->core->context_set_data(extension_context, nullptr);
}

extern "C" void native_extension_tick(const NativeExtensionHostApi* host, void* extension_context,
                                       uint32_t instance_id) {
    if (!host || !host->core || !host->binding || !host->canvas) return;
    PackageState* state = static_cast<PackageState*>(host->core->context_get_data(extension_context));
    InstanceState* instance = find_instance(state, instance_id);
    if (!instance || !instance->canvas || !instance->sprite_buffer) return;
    const uint32_t now = host->core->millis();
    bool changed_digits[MAX_CLOCK_DIGITS] = {};
    if (now >= instance->next_clock_resolve_ms) {
        char resolved[TIME_TEMPLATE_CAPACITY] = {};
        char normalized[CLOCK_TEXT_CAPACITY] = {};
        if (host->binding->resolve(extension_context, instance_id, instance->time_template, resolved, sizeof(resolved)) &&
            normalize_clock(resolved, normalized) && !text_equals(normalized, instance->clock)) {
            const uint8_t old_count = static_cast<uint8_t>(instance->clock[4] ? 6 : 4);
            const uint8_t new_count = static_cast<uint8_t>(normalized[4] ? 6 : 4);
            for (uint8_t index = 0; index < new_count; ++index)
                changed_digits[index] = old_count != new_count || instance->clock[index] != normalized[index];
            copy_text(instance->clock, sizeof(instance->clock), normalized);
            if (old_count != new_count) instance->render_dirty = true;
            for (uint8_t index = 0; index < new_count; ++index)
                if (changed_digits[index] && instance->transition_intensity && instance->transition_ms)
                    instance->transition_started_ms[index] = now;
        }
        instance->next_clock_resolve_ms = now + CLOCK_RESOLVE_MS;
    }
    if (!instance->flicker_ends_ms && instance->next_flicker_ms && now >= instance->next_flicker_ms) {
        const uint8_t digit_count = static_cast<uint8_t>(instance->clock[4] ? 6 : 4);
        const uint8_t token_count = token_count_for_digits(digit_count);
        uint8_t candidate = static_cast<uint8_t>(next_random(instance) % token_count);
        bool found = false;
        for (uint8_t attempt = 0; attempt < token_count; ++attempt) {
            const uint8_t token = static_cast<uint8_t>((candidate + attempt) % token_count);
            if (token_is_separator(digit_count, token)) {
                instance->flicker_token = token;
                instance->flicker_ends_ms = now + instance->flicker_duration_ms;
                found = true;
                break;
            }
            const uint8_t digit = token_digit_index(digit_count, token);
            if (instance->transition_started_ms[digit] && now - instance->transition_started_ms[digit] < instance->transition_ms)
                continue;
            instance->flicker_token = token;
            instance->flicker_ends_ms = now + instance->flicker_duration_ms;
            found = true;
            break;
        }
        schedule_flicker(instance, now);
        if (!found) instance->flicker_ends_ms = 0;
    }
    if (instance->render_dirty) {
        draw_clock(host, instance, true, now);
        return;
    }
    const uint8_t digit_count = instance->rendered_digit_count;
    for (uint8_t index = 0; index < digit_count; ++index) {
        const bool transition_active = instance->transition_started_ms[index] &&
            now - instance->transition_started_ms[index] < instance->transition_ms;
        const bool transition_finished = instance->transition_started_ms[index] && !transition_active;
        const bool flickering_digit = instance->flicker_ends_ms > now &&
            !token_is_separator(digit_count, instance->flicker_token) &&
            token_digit_index(digit_count, instance->flicker_token) == index;
        if (changed_digits[index] || transition_active || transition_finished ||
            flickering_digit)
            redraw_digit(host, instance, index, now);
        if (transition_finished) instance->transition_started_ms[index] = 0;
    }
    if (instance->flicker_ends_ms && now >= instance->flicker_ends_ms) {
        if (token_is_separator(digit_count, instance->flicker_token))
            redraw_token(host, instance, instance->flicker_token, now);
        else
            redraw_digit(host, instance, token_digit_index(digit_count, instance->flicker_token), now);
        instance->flicker_ends_ms = 0;
    }
    // The host still schedules the configured 50 ms tick, but an idle widget
    // does no canvas work. Breathing is the only effect that needs its own
    // continuous redraw; strike and flicker redraw only their affected digit.
    const uint16_t separator_brightness_now = separator_brightness(instance, 2, now);
    if (instance->separator_intensity && instance->separator_period_ms >= 2u &&
        separator_brightness_now != instance->rendered_separator_brightness) {
        for (uint8_t token = 0; token < instance->layout.token_count; ++token)
            if (token_is_separator(digit_count, token)) redraw_token(host, instance, token, now);
        instance->rendered_separator_brightness = separator_brightness_now;
    }
    if (instance->flicker_ends_ms > now && token_is_separator(digit_count, instance->flicker_token))
        redraw_token(host, instance, instance->flicker_token, now);
}