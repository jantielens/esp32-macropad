#include "native_extension_api.h"

extern "C" const NativeExtensionDescriptor native_extension_descriptor = {
    NATIVE_EXTENSION_DESCRIPTOR_MAGIC, NATIVE_EXTENSION_ABI_VERSION,
    NATIVE_EXTENSION_TARGET_ABI, "nixie-clock", "1.0.0", "Nixie Clock",
    250, 0,
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
    uint8_t separators_visible;
    uint8_t previous_separators_visible;
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

void draw_token(const NativeExtensionHostApi* host, InstanceState* instance, uint8_t digit_count,
                uint8_t token, bool separators_visible) {
    const bool separator = token_is_separator(digit_count, token);
    const int16_t x = instance->layout.positions[token];
    if (separator && !separators_visible) {
        host->canvas->canvas_fill_rect(instance->canvas, x, instance->layout.top,
                                       instance->layout.digit_width, instance->layout.digit_height, BLACK);
        return;
    }
    const uint8_t sprite = separator ? 10 : static_cast<uint8_t>(instance->clock[token_digit_index(digit_count, token)] - '0');
    if (!decode_sprite(instance, sprite)) return;
    host->canvas->canvas_blit_rgb565(instance->canvas, x, instance->layout.top, instance->sprite_buffer,
                                     NIXIE_SPRITE_WIDTH, NIXIE_SPRITE_HEIGHT,
                                     instance->layout.digit_width, instance->layout.digit_height);
}

void draw_clock(const NativeExtensionHostApi* host, InstanceState* instance, bool full_clear) {
    const uint8_t digit_count = static_cast<uint8_t>(instance->clock[0] ?
        (instance->clock[4] ? 6 : 4) : 0);
    if (!digit_count || !build_layout(instance, digit_count)) return;
    if (full_clear) host->canvas->canvas_clear(instance->canvas, BLACK);
    for (uint8_t token = 0; token < instance->layout.token_count; ++token)
        draw_token(host, instance, digit_count, token, instance->separators_visible);
    host->canvas->canvas_invalidate_rect(instance->canvas, 0, 0, instance->width, instance->height);
    copy_text(instance->rendered_clock, sizeof(instance->rendered_clock), instance->clock);
    instance->previous_separators_visible = instance->separators_visible;
    instance->render_dirty = false;
}

void redraw_separators(const NativeExtensionHostApi* host, InstanceState* instance) {
    const uint8_t digit_count = static_cast<uint8_t>(instance->clock[4] ? 6 : 4);
    for (uint8_t token = 0; token < instance->layout.token_count; ++token) {
        if (!token_is_separator(digit_count, token)) continue;
        draw_token(host, instance, digit_count, token, instance->separators_visible);
        host->canvas->canvas_invalidate_rect(instance->canvas, instance->layout.positions[token], instance->layout.top,
                                             instance->layout.digit_width, instance->layout.digit_height);
    }
    instance->previous_separators_visible = instance->separators_visible;
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
    instance->separators_visible = static_cast<uint8_t>((host->core->millis() / 500u) % 2u == 0u);
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
    const uint8_t separators_visible = static_cast<uint8_t>((now / 500u) % 2u == 0u);
    if (now >= instance->next_clock_resolve_ms) {
        char resolved[TIME_TEMPLATE_CAPACITY] = {};
        char normalized[CLOCK_TEXT_CAPACITY] = {};
        if (host->binding->resolve(extension_context, instance_id, instance->time_template, resolved, sizeof(resolved)) &&
            normalize_clock(resolved, normalized) && !text_equals(normalized, instance->clock)) {
            copy_text(instance->clock, sizeof(instance->clock), normalized);
            instance->render_dirty = true;
        }
        instance->next_clock_resolve_ms = now + CLOCK_RESOLVE_MS;
    }
    instance->separators_visible = separators_visible;
    if (instance->render_dirty) draw_clock(host, instance, true);
    else if (instance->separators_visible != instance->previous_separators_visible) redraw_separators(host, instance);
}