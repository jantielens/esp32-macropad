#include "../board_config.h"

#if HAS_DISPLAY && HAS_PROGRESS_WIDGET

#include "widget.h"

#include "../log_manager.h"
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <new>
#include <stdlib.h>

#if HAS_LVGL_EPAPER
int display_manager_get_presentation_mode();
#endif

namespace progress_widget {

// This is a measurement candidate, not a hardware performance guarantee.
constexpr uint16_t candidate_limit = 1024;
constexpr uint32_t frame_interval = 33;

enum class Layout : uint8_t { Grid, Horizontal, Vertical };
enum class Shape : uint8_t { Circle, Block };
enum class Inactive : uint8_t { Auto, Filled, Outline, Hidden };
enum class Partial : uint8_t { Whole, Size, Fill };
enum class Order : uint8_t {
    Rows, Columns, SnakeRows, SnakeColumns, SpiralIn, SpiralOut,
    CenterOut, EdgesIn, Diagonal, Random
};
enum class Origin : uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };
enum class Direction : uint8_t { Auto, Left, Right, Up, Down };
enum class Effect : uint8_t { None, Fade, Grow, Pop, Wipe, Flip };

constexpr const char* layouts[] = {"grid", "horizontal", "vertical"};
constexpr const char* shapes[] = {"circle", "block"};
constexpr const char* inactive_styles[] = {"auto", "filled", "outline", "hidden"};
constexpr const char* partials[] = {"whole", "size", "fill"};
constexpr const char* orders[] = {"rows", "columns", "snake_rows", "snake_columns",
    "spiral_in", "spiral_out", "center_out", "edges_in", "diagonal", "random"};
constexpr const char* origins[] = {"top_left", "top_right", "bottom_left", "bottom_right"};
constexpr const char* directions[] = {"auto", "left", "right", "up", "down"};
constexpr const char* effects[] = {"none", "fade", "grow", "pop", "wipe", "flip"};
constexpr const char* value_fields[] = {
    "widget_data_binding", "widget_data_binding_2", "widget_data_binding_3", "widget_data_binding_4"
};
constexpr const char* color_fields[] = {
    "widget_progress_active_color", "widget_progress_active_color_2",
    "widget_progress_active_color_3", "widget_progress_active_color_4"
};
constexpr const char* color_defaults[] = {
    "auto", DEFAULT_WIDGET_SERIES_COLOR_2, DEFAULT_WIDGET_SERIES_COLOR_3, DEFAULT_WIDGET_SERIES_COLOR_4
};

struct Config {
    char minimum[CONFIG_BINDABLE_SHORT_LEN];
    char maximum[CONFIG_BINDABLE_SHORT_LEN];
    char units[CONFIG_BINDABLE_SHORT_LEN];
    char active_color[MAX_WIDGET_BINDINGS][CONFIG_COLOR_MAX_LEN];
    char inactive_color[CONFIG_COLOR_MAX_LEN];
    float aspect;
    uint32_t seed;
    uint16_t columns, duration, stagger;
    int16_t gap_x, gap_y;
    uint8_t thickness, rounding, inactive_size, inactive_outline_width;
    Layout layout;
    Shape shape;
    Inactive inactive;
    Partial partial;
    Order order;
    Origin origin;
    Direction direction;
    Effect effect;
    bool invert, match_spacing, valid;
};

struct Motion {
    float from, to, phase_from, phase_to;
    uint32_t started;
    uint16_t delay;
    bool running;
};

struct ColorMotion {
    float from[MAX_WIDGET_BINDINGS - 1], to[MAX_WIDGET_BINDINGS - 1];
    uint32_t started;
    bool running;
};

struct State {
    lv_obj_t* object;
    lv_obj_t* parent;
    const Config* config;
    uint16_t* slots;
    Motion* motion;
    uint16_t count, columns, rows;
    int32_t width, height, cell_w, cell_h, gap_x, gap_y, left, top;
    uint32_t active_color[MAX_WIDGET_BINDINGS], inactive_color, last_frame;
    ColorMotion colors;
    double target;
    const char* error;
    uint8_t series_count;
    bool active, valid, layout_dirty, monochrome;
};

static_assert(sizeof(Config) <= WIDGET_CONFIG_MAX_BYTES, "Progress config too large");
static_assert(sizeof(State) <= WIDGET_STATE_MAX_BYTES, "Progress state too large");

template<size_t N>
int choice(const char* text, const char* const (&values)[N]) {
    if (!text) return -1;
    for (size_t i = 0; i < N; ++i) if (!strcmp(text, values[i])) return static_cast<int>(i);
    return -1;
}

bool number(const char* text, double* value) {
    if (!text) return false;
    while (isspace(static_cast<unsigned char>(*text))) ++text;
    char* end = nullptr;
    const double parsed = strtod(text, &end);
    if (end == text || !isfinite(parsed)) return false;
    while (isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end) return false;
    *value = parsed;
    return true;
}

bool resolve(const char* text, char* result, size_t size) {
    return text && binding_template_resolve_checked(text, result, size);
}

bool resolve_number(const char* text, double* value) {
    char result[BINDING_TEMPLATE_MAX_LEN];
    return resolve(text, result, sizeof(result)) && number(result, value);
}

bool resolve_color(const char* text, uint32_t* color) {
    char result[BINDING_TEMPLATE_MAX_LEN];
    if (!resolve(text, result, sizeof(result))) return false;
    const char* start = result;
    while (isspace(static_cast<unsigned char>(*start))) ++start;
    return parse_hex_color(start, color);
}

bool is_auto(JsonVariantConst value) {
    return value.isNull() || (value.is<const char*>() &&
        (!strcmp(value.as<const char*>(), "auto") || !value.as<const char*>()[0]));
}

bool valid_binding_field(JsonVariantConst value, size_t limit, bool color) {
    if (value.isNull()) return true;
    if (value.is<const char*>()) {
        const char* text = value.as<const char*>();
        if (strlen(text) >= limit || !*text) return false;
        if (color && !strcmp(text, "auto")) return true;
        if (binding_template_has_bindings(text)) return true;
        if (color) { uint32_t rgb; return parse_hex_color(text, &rgb); }
        double parsed;
        return number(text, &parsed);
    }
    return !color && !value.is<bool>() && value.is<double>() && isfinite(value.as<double>());
}

bool valid_integer(JsonVariantConst value, int minimum, int maximum, bool automatic = false) {
    if (automatic && is_auto(value)) return true;
    return value.isNull() || (!value.is<bool>() && value.is<int>() &&
        value.as<int>() >= minimum && value.as<int>() <= maximum);
}

bool literal_number(JsonVariantConst value, double fallback, double* output) {
    if (value.isNull()) { *output = fallback; return true; }
    if (value.is<const char*>()) {
        const char* text = value.as<const char*>();
        return !binding_template_has_bindings(text) && number(text, output);
    }
    *output = value.as<double>();
    return true;
}

template<size_t N>
bool valid_choice(JsonVariantConst value, const char* const (&choices)[N]) {
    return value.isNull() || (value.is<const char*>() && choice(value.as<const char*>(), choices) >= 0);
}

const char* validate(JsonObjectConst btn) {
    for (const char* field : {"widget_progress_min", "widget_progress_max", "widget_progress_units"}) {
        if (!valid_binding_field(btn[field], CONFIG_BINDABLE_SHORT_LEN, false))
            return "Progress range and count must be numbers or bindings of at most 63 bytes";
    }
    for (const char* field : color_fields) {
        if (!valid_binding_field(btn[field], CONFIG_COLOR_MAX_LEN, true))
            return "Progress colors must be Auto, colors, or bindings of at most 191 bytes";
    }
    if (!valid_binding_field(btn["widget_progress_inactive_color"], CONFIG_COLOR_MAX_LEN, true))
        return "Progress colors must be Auto, colors, or bindings of at most 191 bytes";
    bool stacked = false;
    for (unsigned i = 0; i < MAX_WIDGET_BINDINGS; ++i) {
        JsonVariantConst value = btn[value_fields[i]];
        if (value.isNull()) continue;
        if (!value.is<const char*>() ||
            (*value.as<const char*>() && !valid_binding_field(value, CONFIG_LABEL_MAX_LEN, false)))
            return "Progress series value must be a number or binding";
        if (i && *value.as<const char*>()) stacked = true;
    }
    double min_value, max_value;
    if (literal_number(btn["widget_progress_min"], 0, &min_value) &&
        literal_number(btn["widget_progress_max"], 100, &max_value) &&
        max_value <= min_value)
        return "Progress maximum must be greater than minimum";
    if (stacked) {
        if (literal_number(btn["widget_progress_min"], 0, &min_value) && min_value != 0)
            return "Stacked Progress requires minimum zero";
        if (btn["widget_progress_invert"] == true)
            return "Stacked Progress cannot be inverted";
        for (const char* field : value_fields) {
            JsonVariantConst value = btn[field];
            if (value.isNull() || !*value.as<const char*>()) continue;
            double amount;
            if (literal_number(value, 0, &amount) && amount < 0)
                return "Stacked Progress requires nonnegative series";
        }
    }
    if (!valid_choice(btn["widget_progress_layout"], layouts) ||
        !valid_choice(btn["widget_progress_shape"], shapes) ||
        !valid_choice(btn["widget_progress_inactive_style"], inactive_styles) ||
        !valid_choice(btn["widget_progress_partial"], partials) ||
        !valid_choice(btn["widget_progress_order"], orders) ||
        !valid_choice(btn["widget_progress_origin"], origins) ||
        !valid_choice(btn["widget_progress_fill_direction"], directions) ||
        !valid_choice(btn["widget_progress_effect"], effects))
        return "Invalid Progress option";
    if (!valid_integer(btn["widget_progress_columns"], 1, candidate_limit, true) ||
        !valid_integer(btn["widget_progress_gap_x"], 0, 256, true) ||
        !valid_integer(btn["widget_progress_gap_y"], 0, 256, true) ||
        !valid_integer(btn["widget_progress_thickness"], 1, 100, true) ||
        !valid_integer(btn["widget_progress_rounding"], 0, 50) ||
        !valid_integer(btn["widget_progress_inactive_size"], 1, 100) ||
        !valid_integer(btn["widget_progress_inactive_outline_width"], 1, 10) ||
        !valid_integer(btn["widget_anim_ms"], 0, 5000) ||
        !valid_integer(btn["widget_progress_stagger_ms"], 0, 1000))
        return "Progress geometry or animation setting is outside its supported range";
    if (btn.containsKey("widget_progress_aspect") &&
        (btn["widget_progress_aspect"].is<bool>() || !btn["widget_progress_aspect"].is<double>() ||
         !isfinite(btn["widget_progress_aspect"].as<double>()) ||
         btn["widget_progress_aspect"].as<double>() < 0.1 ||
         btn["widget_progress_aspect"].as<double>() > 10))
        return "Progress aspect must be between 0.1 and 10";
    if (btn.containsKey("widget_progress_seed") &&
        (btn["widget_progress_seed"].is<bool>() || !btn["widget_progress_seed"].is<uint32_t>()))
        return "Progress seed must be an unsigned 32-bit integer";
    if (btn.containsKey("widget_progress_invert") && !btn["widget_progress_invert"].is<bool>())
        return "Progress inversion must be boolean";
    if (btn.containsKey("widget_progress_match_spacing") && !btn["widget_progress_match_spacing"].is<bool>())
        return "Progress spacing match must be boolean";
    for (const char* field : {"widget_progress_units"}) {
        JsonVariantConst value = btn[field];
        if (!value.isNull() && !(value.is<const char*>() &&
            binding_template_has_bindings(value.as<const char*>()))) {
            double count;
            char text[64];
            if (value.is<const char*>()) strlcpy(text, value.as<const char*>(), sizeof(text));
            else snprintf(text, sizeof(text), "%.17g", value.as<double>());
            if (!number(text, &count) || count < 1 || count > candidate_limit || floor(count) != count)
                return "Progress unit count must be a supported positive integer";
        }
    }
    return nullptr;
}

int auto_int(JsonVariant value, int fallback) {
    return is_auto(value) ? fallback : value.as<int>();
}

void parse_number_field(JsonVariant value, char* output, size_t size, const char* fallback) {
    if (value.is<double>() && !value.is<const char*>())
        snprintf(output, size, "%.17g", value.as<double>());
    else
        widget_parse_field(value, output, size, fallback, false);
}

void parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = new (data) Config{};
    if (const char* error = validate(btn)) {
        LOGW("Progress", "Invalid configuration: %s", error);
        return;
    }
    parse_number_field(btn["widget_progress_min"], cfg->minimum, sizeof(cfg->minimum), "0");
    parse_number_field(btn["widget_progress_max"], cfg->maximum, sizeof(cfg->maximum), "100");
    parse_number_field(btn["widget_progress_units"], cfg->units, sizeof(cfg->units), "25");
    for (unsigned i = 0; i < MAX_WIDGET_BINDINGS; ++i)
        widget_parse_field(btn[color_fields[i]], cfg->active_color[i], sizeof(cfg->active_color[i]), color_defaults[i]);
    widget_parse_field(btn["widget_progress_inactive_color"], cfg->inactive_color, sizeof(cfg->inactive_color), "auto");
    cfg->layout = static_cast<Layout>(choice(btn["widget_progress_layout"] | "grid", layouts));
    cfg->shape = static_cast<Shape>(choice(btn["widget_progress_shape"] | "circle", shapes));
    cfg->inactive = static_cast<Inactive>(choice(btn["widget_progress_inactive_style"] | "auto", inactive_styles));
    cfg->partial = static_cast<Partial>(choice(btn["widget_progress_partial"] | "size", partials));
    cfg->order = static_cast<Order>(choice(btn["widget_progress_order"] | "rows", orders));
    cfg->origin = static_cast<Origin>(choice(btn["widget_progress_origin"] | "top_left", origins));
    cfg->direction = static_cast<Direction>(choice(btn["widget_progress_fill_direction"] | "auto", directions));
    cfg->effect = static_cast<Effect>(choice(btn["widget_progress_effect"] | "grow", effects));
    cfg->aspect = btn["widget_progress_aspect"] | 1.0f;
    cfg->seed = btn["widget_progress_seed"] | uint32_t(84721);
    cfg->columns = auto_int(btn["widget_progress_columns"], 0);
    cfg->gap_x = auto_int(btn["widget_progress_gap_x"], -1);
    cfg->gap_y = auto_int(btn["widget_progress_gap_y"], -1);
    cfg->thickness = auto_int(btn["widget_progress_thickness"], 0);
    cfg->rounding = btn["widget_progress_rounding"] | 0;
    cfg->inactive_size = btn["widget_progress_inactive_size"] | 25;
    cfg->inactive_outline_width = btn["widget_progress_inactive_outline_width"] | 2;
    cfg->duration = btn["widget_anim_ms"] | 300;
    cfg->stagger = btn["widget_progress_stagger_ms"] | 0;
    cfg->invert = btn["widget_progress_invert"] | false;
    cfg->match_spacing = btn["widget_progress_match_spacing"] | true;
    cfg->valid = true;
}

void visit(const uint8_t* data, WidgetResourceVisitor visitor, void* context) {
    const auto& cfg = *reinterpret_cast<const Config*>(data);
    visitor(WidgetResourceKind::Binding, "widget_progress_min", 0, cfg.minimum, context);
    visitor(WidgetResourceKind::Binding, "widget_progress_max", 0, cfg.maximum, context);
    visitor(WidgetResourceKind::Binding, "widget_progress_units", 0, cfg.units, context);
    for (unsigned i = 0; i < MAX_WIDGET_BINDINGS; ++i)
        visitor(WidgetResourceKind::Binding, color_fields[i], i, cfg.active_color[i], context);
    visitor(WidgetResourceKind::Binding, "widget_progress_inactive_color", 0, cfg.inactive_color, context);
}

struct SortSlot { uint16_t slot; uint32_t key, tie; };

int compare_slot(const void* first, const void* second) {
    const auto& a = *static_cast<const SortSlot*>(first);
    const auto& b = *static_cast<const SortSlot*>(second);
    if (a.key != b.key) return a.key < b.key ? -1 : 1;
    return a.tie < b.tie ? -1 : a.tie > b.tie ? 1 : 0;
}

uint32_t random_next(uint32_t& seed) {
    seed += 0x9e3779b9u;
    uint32_t x = seed;
    x = (x ^ (x >> 16)) * 0x85ebca6bu;
    x = (x ^ (x >> 13)) * 0xc2b2ae35u;
    return x ^ (x >> 16);
}

uint16_t transformed_slot(const State& state, int x, int y) {
    if (state.config->origin == Origin::TopRight || state.config->origin == Origin::BottomRight)
        x = state.columns - 1 - x;
    if (state.config->origin == Origin::BottomLeft || state.config->origin == Origin::BottomRight)
        y = state.rows - 1 - y;
    return static_cast<uint16_t>(y * state.columns + x);
}

bool make_order(State* state) {
    auto* entries = static_cast<SortSlot*>(lv_malloc(sizeof(SortSlot) * state->count));
    if (!entries) return false;
    const int columns = state->columns, rows = state->rows;
    const auto order = state->config->order;
    unsigned used = 0;
    auto append = [&](int x, int y, uint32_t key) {
        const int base = y * columns + x;
        const uint16_t slot = transformed_slot(*state, x, y);
        if (slot >= state->count) return;
        entries[used++] = {slot, key, static_cast<uint32_t>(base)};
    };
    if (order == Order::SpiralIn || order == Order::SpiralOut) {
        int left = 0, right = columns - 1, top = 0, bottom = rows - 1;
        uint32_t rank = 0;
        while (left <= right && top <= bottom) {
            for (int x = left; x <= right; ++x) append(x, top, rank++);
            ++top;
            for (int y = top; y <= bottom; ++y) append(right, y, rank++);
            --right;
            if (top <= bottom) { for (int x = right; x >= left; --x) append(x, bottom, rank++); --bottom; }
            if (left <= right) { for (int y = bottom; y >= top; --y) append(left, y, rank++); ++left; }
        }
    } else {
        uint32_t seed = state->config->seed;
        for (int y = 0; y < rows; ++y) for (int x = 0; x < columns; ++x) {
            uint32_t key = y * columns + x;
            if (order == Order::Columns) key = x * rows + y;
            else if (order == Order::SnakeRows) key = y * columns + (y % 2 ? columns - 1 - x : x);
            else if (order == Order::SnakeColumns) key = x * rows + (x % 2 ? rows - 1 - y : y);
            else if (order == Order::Diagonal) key = (x + y) * (rows + 1) + y;
            else if (order == Order::CenterOut || order == Order::EdgesIn) {
                const int dx = 2 * x - (columns - 1), dy = 2 * y - (rows - 1);
                key = static_cast<uint32_t>(dx * dx + dy * dy);
                if (order == Order::EdgesIn) key = UINT32_MAX - key;
            } else if (order == Order::Random) key = random_next(seed);
            append(x, y, key);
        }
    }
    qsort(entries, used, sizeof(SortSlot), compare_slot);
    for (unsigned rank = 0; rank < used; ++rank) {
        const unsigned source = order == Order::SpiralOut ? used - 1 - rank : rank;
        state->slots[rank] = entries[source].slot;
    }
    lv_free(entries);
    return used == state->count;
}

void fail(State* state, const char* message) {
    if (!state->error || strcmp(state->error, message)) LOGW("Progress", "%s", message);
    const bool changed = state->valid || !state->error || strcmp(state->error, message);
    state->error = message;
    state->valid = false;
    if (changed && state->object) lv_obj_invalidate(state->object);
}

bool layout(State* state, uint16_t count) {
    const Config& cfg = *state->config;
    const int width = lv_obj_get_content_width(state->parent);
    const int height = lv_obj_get_content_height(state->parent);
    if (width <= 0 || height <= 0) { fail(state, "Insufficient widget space"); return false; }
    lv_obj_set_size(state->object, width, height);
    lv_obj_set_pos(state->object, 0, 0);
    int columns = cfg.layout == Layout::Horizontal ? count :
        cfg.layout == Layout::Vertical ? 1 : cfg.columns;
    if (!columns) {
        const float aspect = cfg.shape == Shape::Circle ? 1 : cfg.aspect;
        columns = static_cast<int>(lroundf(sqrtf(count * width / (height * aspect))));
        columns = clamp_val(columns, 1, static_cast<int>(count));
    }
    columns = clamp_val(columns, 1, static_cast<int>(count));
    const int rows = (count + columns - 1) / columns;
    const int auto_gap = static_cast<int>(fminf(width / static_cast<float>(columns),
                                                height / static_cast<float>(rows)) * 0.2f);
    int gap_x = cfg.gap_x < 0 ? auto_gap : cfg.gap_x;
    int gap_y = cfg.gap_y < 0 ? auto_gap : cfg.gap_y;
    if (cfg.gap_x < 0 && columns > 1)
        gap_x = clamp_val(gap_x, 0, (width - columns) / (columns - 1) > 0 ? (width - columns) / (columns - 1) : 0);
    if (cfg.gap_y < 0 && rows > 1)
        gap_y = clamp_val(gap_y, 0, (height - rows) / (rows - 1) > 0 ? (height - rows) / (rows - 1) : 0);
    const int available_w = width - (columns - 1) * gap_x;
    const int available_h = height - (rows - 1) * gap_y;
    if (available_w < columns || available_h < rows) { fail(state, "Insufficient widget space"); return false; }
    float cell_w = available_w / static_cast<float>(columns);
    float cell_h = available_h / static_cast<float>(rows);
    if (cfg.layout == Layout::Grid || cfg.shape == Shape::Circle) {
        const float aspect = cfg.shape == Shape::Circle ? 1 : cfg.aspect;
        cell_w = fminf(cell_w, cell_h * aspect);
        cell_h = cell_w / aspect;
    } else if (cfg.thickness) {
        if (cfg.layout == Layout::Horizontal) cell_h *= cfg.thickness / 100.0f;
        else cell_w *= cfg.thickness / 100.0f;
    }
    int cw = static_cast<int>(floorf(cell_w)), ch = static_cast<int>(floorf(cell_h));
    if (cw < 1 || ch < 1) { fail(state, "Insufficient widget space"); return false; }
    if (!cfg.match_spacing && cfg.gap_x < 0 && cfg.gap_y < 0) {
        if (columns > 1) gap_x = (width - columns * cw) / (columns - 1);
        if (rows > 1) gap_y = (height - rows * ch) / (rows - 1);
    }
    const bool rebuild = state->count != count || state->columns != columns || state->rows != rows || !state->slots;
    state->width = width; state->height = height;
    state->columns = columns; state->rows = rows;
    state->cell_w = cw; state->cell_h = ch; state->gap_x = gap_x; state->gap_y = gap_y;
    state->left = (width - (columns * cw + (columns - 1) * gap_x)) / 2;
    state->top = (height - (rows * ch + (rows - 1) * gap_y)) / 2;
    if (rebuild) {
        lv_free(state->slots); state->slots = nullptr;
        lv_free(state->motion); state->motion = nullptr;
        state->count = count;
        state->slots = static_cast<uint16_t*>(lv_malloc(sizeof(uint16_t) * count));
        if (!state->slots || !make_order(state)) {
            lv_free(state->slots); state->slots = nullptr;
            fail(state, "Progress allocation failed");
            return false;
        }
        state->valid = false;
    }
    state->layout_dirty = false;
    return true;
}

float target_for(const State& state, unsigned rank, double target) {
    const double amount = clamp_val(target - rank, 0.0, 1.0);
    if (state.config->partial == Partial::Whole) return amount >= 1 ? 1 : 0;
    return static_cast<float>(amount);
}

struct Sample { float value, phase; };
float transition(uint32_t elapsed, uint16_t duration) {
    const float t = duration ? clamp_val(elapsed / static_cast<float>(duration), 0.0f, 1.0f) : 1;
    return t * t * (3 - 2 * t);
}

Sample sample(const Motion& motion, uint32_t now, uint16_t duration) {
    if (!motion.running) return {motion.to, motion.phase_to};
    const uint32_t elapsed = now - motion.started;
    if (elapsed < motion.delay) return {motion.from, motion.phase_from};
    const float eased = transition(elapsed - motion.delay, duration);
    return {motion.from + (motion.to - motion.from) * eased,
            motion.phase_from + (motion.phase_to - motion.phase_from) * eased};
}

bool animate(const State& state) {
    return state.valid && state.config->effect != Effect::None && state.config->duration &&
        !DISPLAY_DISABLE_ANIMATIONS && !HAS_LVGL_EPAPER;
}

float color_boundary(const State& state, unsigned series, uint32_t now) {
    const ColorMotion& motion = state.colors;
    const float t = motion.running ? transition(now - motion.started, state.config->duration) : 1;
    return motion.from[series] + (motion.to[series] - motion.from[series]) * t;
}

bool retarget_colors(State* state, const float* boundaries, uint32_t now) {
    bool changed = false;
    for (unsigned i = 0; i < MAX_WIDGET_BINDINGS - 1; ++i)
        changed |= boundaries[i] != state->colors.to[i];
    if (!changed && state->valid) return false;
    bool visible = state->target > 0;
    for (unsigned i = 0; !visible && state->motion && i < state->count; ++i) {
        const Sample current = sample(state->motion[i], now, state->config->duration);
        visible = current.value > 0 && current.phase > 0;
    }
    for (unsigned i = 0; i < MAX_WIDGET_BINDINGS - 1; ++i) {
        state->colors.from[i] = color_boundary(*state, i, now);
        state->colors.to[i] = boundaries[i];
    }
    state->colors.started = now;
    state->colors.running = changed && visible && animate(*state);
    return true;
}

void retarget(State* state, double target, uint32_t now) {
    const Config& cfg = *state->config;
    if (!animate(*state)) {
        lv_free(state->motion); state->motion = nullptr;
        state->target = target;
        return;
    }
    if (!state->motion) {
        state->motion = static_cast<Motion*>(lv_malloc(sizeof(Motion) * state->count));
        if (!state->motion) { fail(state, "Progress animation allocation failed"); return; }
        for (unsigned i = 0; i < state->count; ++i) {
            const float value = target_for(*state, i, state->target);
            state->motion[i] = {value, value, value > 0 ? 1.0f : 0.0f,
                                value > 0 ? 1.0f : 0.0f, now, 0, false};
        }
    }
    unsigned changed = 0;
    for (unsigned i = 0; i < state->count; ++i)
        if (target_for(*state, i, state->target) != target_for(*state, i, target)) ++changed;
    unsigned sequence = 0;
    const bool decreasing = target < state->target;
    for (unsigned j = 0; j < state->count; ++j) {
        const unsigned i = decreasing ? state->count - 1 - j : j;
        const float old_target = target_for(*state, i, state->target);
        const float new_target = target_for(*state, i, target);
        if (old_target == new_target) continue;
        Motion& motion = state->motion[i];
        const Sample current = sample(motion, now, cfg.duration);
        const uint16_t delay = changed > 1 ? static_cast<uint16_t>(sequence * cfg.stagger / (changed - 1)) : 0;
        ++sequence;
        float from = current.value, to = new_target;
        if (cfg.effect == Effect::Pop) {
            if (current.phase == 0) from = new_target;
            if (new_target == 0) to = from;
        }
        motion = {from, to, current.phase, new_target > 0 ? 1.0f : 0.0f,
                  now, delay, true};
    }
    state->target = target;
}

Direction fill_direction(const Config& cfg) {
    if (cfg.direction != Direction::Auto) return cfg.direction;
    if (cfg.layout == Layout::Vertical)
        return cfg.origin == Origin::BottomLeft || cfg.origin == Origin::BottomRight ? Direction::Up : Direction::Down;
    return cfg.origin == Origin::TopRight || cfg.origin == Origin::BottomRight ? Direction::Left : Direction::Right;
}

void draw_shape(lv_layer_t* layer, lv_area_t area, const Config& cfg, uint32_t color,
                uint8_t opacity, bool outline) {
    if (area.x2 < area.x1 || area.y2 < area.y1 || !opacity) return;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = cfg.shape == Shape::Circle ? LV_RADIUS_CIRCLE :
        LV_MIN(lv_area_get_width(&area), lv_area_get_height(&area)) * cfg.rounding / 100;
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = outline ? static_cast<uint8_t>(LV_OPA_TRANSP) : opacity;
    dsc.border_color = lv_color_hex(color);
    dsc.border_opa = opacity;
    dsc.border_width = outline ? LV_MIN(cfg.inactive_outline_width,
        LV_MIN(lv_area_get_width(&area), lv_area_get_height(&area))) : 0;
    lv_draw_rect(layer, &dsc, &area);
}

lv_area_t scaled_area(lv_area_t area, float sx, float sy) {
    const int w = lv_area_get_width(&area), h = lv_area_get_height(&area);
    const int nw = clamp_val(static_cast<int>(lroundf(w * sx)), 0, w);
    const int nh = clamp_val(static_cast<int>(lroundf(h * sy)), 0, h);
    area.x1 += (w - nw) / 2; area.y1 += (h - nh) / 2;
    area.x2 = area.x1 + nw - 1; area.y2 = area.y1 + nh - 1;
    return area;
}

void clip_segment(lv_layer_t* layer, const lv_area_t& area, float start, float end, Direction direction) {
    const int w = lv_area_get_width(&area), h = lv_area_get_height(&area);
    const int length = direction == Direction::Up || direction == Direction::Down ? h : w;
    const int first = static_cast<int>(lroundf(length * start));
    const int last = static_cast<int>(lroundf(length * end));
    lv_area_t clip = area;
    if (direction == Direction::Right) { clip.x1 += first; clip.x2 = area.x1 + last - 1; }
    else if (direction == Direction::Left) { clip.x1 = area.x2 - last + 1; clip.x2 -= first; }
    else if (direction == Direction::Down) { clip.y1 += first; clip.y2 = area.y1 + last - 1; }
    else { clip.y1 = area.y2 - last + 1; clip.y2 -= first; }
    layer->_clip_area.x1 = LV_MAX(layer->_clip_area.x1, clip.x1);
    layer->_clip_area.y1 = LV_MAX(layer->_clip_area.y1, clip.y1);
    layer->_clip_area.x2 = LV_MIN(layer->_clip_area.x2, clip.x2);
    layer->_clip_area.y2 = LV_MIN(layer->_clip_area.y2, clip.y2);
}

void clip_fill(lv_layer_t* layer, const lv_area_t& area, float amount, Direction direction) {
    clip_segment(layer, area, 0, amount, direction);
}

void draw(lv_event_t* event) {
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    lv_layer_t* layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(state->object, &bounds);
    if (!state->valid) {
        lv_draw_label_dsc_t label;
        lv_draw_label_dsc_init(&label);
        label.color = lv_obj_get_style_text_color(state->parent, LV_PART_MAIN);
        label.font = lv_obj_get_style_text_font(state->parent, LV_PART_MAIN);
        label.text = state->error ? state->error : "Progress unavailable";
        label.text_static = 1;
        label.align = LV_TEXT_ALIGN_CENTER;
        lv_draw_label(layer, &label, &bounds);
        return;
    }
    const Config& cfg = *state->config;
    const Inactive inactive = cfg.inactive == Inactive::Auto ?
        (state->monochrome ? Inactive::Outline : Inactive::Filled) : cfg.inactive;
    const uint32_t now = lv_tick_get();
    float boundaries[MAX_WIDGET_BINDINGS - 1];
    for (unsigned i = 0; i + 1 < state->series_count; ++i)
        boundaries[i] = color_boundary(*state, i, now);
    for (unsigned rank = 0; rank < state->count; ++rank) {
        const int slot = state->slots[rank], col = slot % state->columns, row = slot / state->columns;
        lv_area_t area{bounds.x1 + state->left + col * (state->cell_w + state->gap_x),
                       bounds.y1 + state->top + row * (state->cell_h + state->gap_y), 0, 0};
        area.x2 = area.x1 + state->cell_w - 1; area.y2 = area.y1 + state->cell_h - 1;
        Sample value{target_for(*state, rank, state->target), 1};
        if (state->motion) value = sample(state->motion[rank], now, cfg.duration);
        const float phase = clamp_val(value.phase, 0.0f, 1.0f);
        const bool flip_back = cfg.effect == Effect::Flip && phase < 0.5f;
        if (inactive != Inactive::Hidden && (value.value < 1 || phase < 1)) {
            float scale = cfg.inactive_size / 100.0f;
            lv_area_t track = scaled_area(area, scale, scale);
            if (cfg.effect == Effect::Flip && state->motion && state->motion[rank].running) {
                if (!flip_back) track.x2 = track.x1 - 1;
                else track = scaled_area(track, 1 - phase * 2, 1);
            }
            const uint8_t opacity = !strcmp(cfg.inactive_color, "auto") && !state->monochrome
                ? 64 : 255;
            draw_shape(layer, track, cfg, state->inactive_color, opacity,
                       inactive == Inactive::Outline);
        }
        if (value.value <= 0 || phase <= 0) continue;
        float active = clamp_val(value.value, 0.0f, 1.0f);
        lv_area_t active_area = area;
        if (cfg.partial == Partial::Size) {
            const float base = inactive == Inactive::Hidden ? 0 : cfg.inactive_size / 100.0f;
            active_area = scaled_area(area, base + (1 - base) * active, base + (1 - base) * active);
        }
        float scale = 1;
        if (cfg.effect == Effect::Grow) scale = phase;
        else if (cfg.effect == Effect::Pop) scale = fminf(1.15f, phase + 0.555f * sinf(3.14159265f * phase));
        if (scale > 1) {
            const int free_left = col ? state->gap_x / 2 : state->left;
            const int free_right = col + 1 < state->columns ? state->gap_x / 2 :
                state->width - state->left - state->columns * state->cell_w -
                    (state->columns - 1) * state->gap_x;
            const int free_top = row ? state->gap_y / 2 : state->top;
            const int free_bottom = row + 1 < state->rows ? state->gap_y / 2 :
                state->height - state->top - state->rows * state->cell_h -
                    (state->rows - 1) * state->gap_y;
            const int dx = LV_MIN(LV_MIN(free_left, free_right),
                static_cast<int>(lroundf(lv_area_get_width(&active_area) * (scale - 1) / 2)));
            const int dy = LV_MIN(LV_MIN(free_top, free_bottom),
                static_cast<int>(lroundf(lv_area_get_height(&active_area) * (scale - 1) / 2)));
            active_area.x1 -= dx; active_area.x2 += dx; active_area.y1 -= dy; active_area.y2 += dy;
        } else if (scale < 1) active_area = scaled_area(active_area, scale, scale);
        if (cfg.effect == Effect::Flip) {
            if (flip_back) continue;
            active_area = scaled_area(active_area, phase * 2 - 1, 1);
        }
        const lv_area_t saved = layer->_clip_area;
        if (cfg.partial == Partial::Fill) clip_fill(layer, active_area, active, fill_direction(cfg));
        if (cfg.effect == Effect::Wipe) clip_fill(layer, active_area, phase, fill_direction(cfg));
        const uint8_t opacity = cfg.effect == Effect::Fade ? static_cast<uint8_t>(lroundf(phase * 255)) : 255;
        const lv_area_t visible = layer->_clip_area;
        float start = 0;
        for (unsigned series = 0; series < state->series_count; ++series) {
            // Size partitions the scaled shape; Fill partitions the full shape before clipping.
            const float end = series + 1 == state->series_count ? 1 :
                clamp_val((boundaries[series] - rank) /
                    (cfg.partial == Partial::Size ? active : 1), 0.0f, 1.0f);
            if (end > start) {
                layer->_clip_area = visible;
                clip_segment(layer, active_area, start, end, fill_direction(cfg));
                draw_shape(layer, active_area, cfg, state->active_color[series], opacity, false);
            }
            start = end;
        }
        layer->_clip_area = saved;
    }
}

void parent_event(lv_event_t* event) {
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    if (lv_event_get_target_obj(event) != state->parent) return;
    if (lv_event_get_code(event) == LV_EVENT_DELETE) state->parent = nullptr;
    else state->layout_dirty = true;
}

void object_deleted(lv_event_t* event) {
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    state->object = nullptr;
}

void create(lv_obj_t* parent, const WidgetConfig* widget, const ScreenButtonConfig*,
            const PadRect*, const UIScaleInfo*, WidgetState* runtime) {
    auto* state = new (runtime->data) State{};
    state->parent = parent;
    state->config = reinterpret_cast<const Config*>(widget->data);
    state->active = true;
    state->layout_dirty = true;
    state->object = lv_obj_create(parent);
    if (!state->object) { LOGE("Progress", "Cannot create drawing object"); return; }
    lv_obj_remove_style_all(state->object);
    lv_obj_remove_flag(state->object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(state->object, LV_OBJ_FLAG_SCROLLABLE);
    if (!lv_obj_add_event_cb(state->object, draw, LV_EVENT_DRAW_MAIN, state) ||
        !lv_obj_add_event_cb(state->object, object_deleted, LV_EVENT_DELETE, state) ||
        !lv_obj_add_event_cb(parent, parent_event, LV_EVENT_SIZE_CHANGED, state) ||
        !lv_obj_add_event_cb(parent, parent_event, LV_EVENT_DELETE, state)) {
        LOGE("Progress", "Cannot register drawing callbacks");
        lv_obj_remove_event_cb_with_user_data(parent, parent_event, state);
        lv_obj_delete(state->object);
        state->object = nullptr;
    }
}

void tick(lv_obj_t*, const WidgetConfig* widget, WidgetState* runtime) {
    auto* state = reinterpret_cast<State*>(runtime->data);
    if (!state->object || !state->parent || !state->active) return;
    const Config& cfg = *state->config;
    if (!cfg.valid) { fail(state, "Invalid Progress configuration"); return; }
    double minimum, maximum, units;
    if (!resolve_number(cfg.minimum, &minimum) || !resolve_number(cfg.maximum, &maximum) ||
        !resolve_number(cfg.units, &units) || maximum <= minimum ||
        units < 1 || units > candidate_limit || floor(units) != units) {
        fail(state, "Progress unavailable");
        return;
    }
    const uint16_t count = static_cast<uint16_t>(units);
    state->series_count = 1;
    for (unsigned i = 1; i < MAX_WIDGET_BINDINGS; ++i)
        if (widget->data_binding[i][0]) state->series_count = i + 1;
    const bool stacked = state->series_count > 1;
    if (stacked && (minimum != 0 || cfg.invert)) {
        fail(state, "Stacked Progress requires minimum zero and no inversion"); return;
    }
    double value = 0;
    float boundaries[MAX_WIDGET_BINDINGS - 1]{};
    for (unsigned i = 0; i < state->series_count; ++i) {
        double amount = 0;
        if (widget->data_binding[i][0] && !resolve_number(widget->data_binding[i], &amount)) {
            fail(state, "Progress unavailable"); return;
        }
        if (stacked && amount < 0) {
            fail(state, "Stacked Progress requires nonnegative series"); return;
        }
        // Saturate before adding so large finite contributions cannot overflow.
        value = stacked ? value + fmin(amount, maximum - value) : amount;
        if (i < MAX_WIDGET_BINDINGS - 1)
            boundaries[i] = stacked ? static_cast<float>(value / maximum * count) : 0;
    }
    for (unsigned i = state->series_count; i < MAX_WIDGET_BINDINGS - 1; ++i)
        boundaries[i] = static_cast<float>(count);
    bool monochrome = false;
#if HAS_LVGL_EPAPER
    monochrome = display_manager_get_presentation_mode() == EPAPER_RENDER_MODE_BW;
#endif
    const lv_color_t foreground = lv_obj_get_style_text_color(state->parent, LV_PART_MAIN);
    uint32_t inactive = lv_color_to_u32(foreground) & 0xFFFFFFu;
    bool color_changed = false;
    for (unsigned i = 0; i < state->series_count; ++i) {
        uint32_t active = inactive;
        if ((i == 0 || widget->data_binding[i][0]) && strcmp(cfg.active_color[i], "auto") &&
            !resolve_color(cfg.active_color[i], &active)) {
            fail(state, "Progress color unavailable"); return;
        }
        color_changed |= state->active_color[i] != active;
        state->active_color[i] = active;
    }
    if (cfg.inactive != Inactive::Hidden) {
        if (strcmp(cfg.inactive_color, "auto")) {
            if (!resolve_color(cfg.inactive_color, &inactive)) { fail(state, "Progress color unavailable"); return; }
        }
    }
    color_changed |= state->inactive_color != inactive || state->monochrome != monochrome;
    state->inactive_color = inactive; state->monochrome = monochrome;
    const int w = lv_obj_get_content_width(state->parent), h = lv_obj_get_content_height(state->parent);
    if (state->layout_dirty || count != state->count || w != state->width || h != state->height || !state->slots) {
        const bool resized = w != state->width || h != state->height || count != state->count;
        if (!layout(state, count)) return;
        if (resized) state->valid = false;
    }
    double target;
    if (value <= minimum) target = 0;
    else if (value >= maximum) target = count;
    else {
        const double denominator = maximum - minimum;
        const double numerator = value - minimum;
        if (isfinite(denominator) && isfinite(numerator) && numerator <= DBL_MAX / count)
            target = numerator * count / denominator;
        else
            target = ((value / 2 - minimum / 2) / (maximum / 2 - minimum / 2)) * count;
        if (!isfinite(target)) { fail(state, "Progress unavailable"); return; }
        target = clamp_val(target, 0.0, static_cast<double>(count));
    }
    if (cfg.invert) target = count - target;
    const bool first = !state->valid;
    bool changed = first || target != state->target;
    if (changed && !first && cfg.partial == Partial::Whole &&
        floor(target) == floor(state->target)) {
        state->target = target;
        changed = false;
    }
    const bool boundaries_changed = retarget_colors(state, boundaries, lv_tick_get());
    if (changed) retarget(state, target, lv_tick_get());
    if (state->error && !state->valid && !first) return;
    if (first) { state->valid = true; state->error = nullptr; }
    if (changed || color_changed || boundaries_changed) lv_obj_invalidate(state->object);
    if (!state->motion && !state->colors.running) return;
    const uint32_t now = lv_tick_get();
    if (now - state->last_frame < frame_interval) return;
    state->last_frame = now;
    bool moving = state->colors.running;
    if (state->colors.running && now - state->colors.started >= cfg.duration)
        state->colors.running = false;
    for (unsigned i = 0; state->motion && i < state->count; ++i) {
        Motion& motion = state->motion[i];
        if (!motion.running) continue;
        if (now - motion.started >= static_cast<uint32_t>(motion.delay) + cfg.duration) motion.running = false;
        moving = true;
    }
    if (moving) lv_obj_invalidate(state->object);
}

void hide(WidgetState* runtime) {
    auto* state = reinterpret_cast<State*>(runtime->data);
    state->active = false;
    state->valid = false;
    state->colors.running = false;
    lv_free(state->motion); state->motion = nullptr;
}

void show(WidgetState* runtime) {
    auto* state = reinterpret_cast<State*>(runtime->data);
    state->active = true;
    state->valid = false;
}

void destroy(WidgetState* runtime) {
    auto* state = reinterpret_cast<State*>(runtime->data);
    if (state->parent) lv_obj_remove_event_cb_with_user_data(state->parent, parent_event, state);
    if (state->object) lv_obj_delete(state->object);
    lv_free(state->slots); state->slots = nullptr;
    lv_free(state->motion); state->motion = nullptr;
    state->active = false;
}

#if HAS_MCP
void describe(JsonObject& out) {
    JsonArray fields = out.createNestedArray("config_fields");
    auto add_choice = [&](const char* name, const char* initial, const auto& options) {
        JsonObject field = fields.createNestedObject();
        field["name"] = name; field["type"] = "string"; field["default"] = initial;
        JsonArray values = field.createNestedArray("enum");
        for (const char* option : options) values.add(option);
    };
    auto numeric = [&](const char* name, double low, double high, double initial) {
        JsonObject field = fields.createNestedObject();
        field["name"] = name; field["type"] = "number"; field["minimum"] = low;
        field["maximum"] = high; field["default"] = initial;
    };
    for (const char* name : {"widget_progress_min", "widget_progress_max", "widget_progress_units"}) {
        JsonObject field = fields.createNestedObject();
        field["name"] = name; field["type"] = "number"; field["bindable"] = true;
        field["max_length"] = CONFIG_BINDABLE_SHORT_LEN - 1;
        field["default"] = !strcmp(name, "widget_progress_min") ? 0 :
            !strcmp(name, "widget_progress_max") ? 100 : 25;
        if (!strcmp(name, "widget_progress_units")) {
            field["minimum"] = 1; field["maximum"] = candidate_limit;
            field["description"] = "Positive integer or binding; the field must fit the available space and memory.";
        }
    }
    for (unsigned i = 0; i < MAX_WIDGET_BINDINGS; ++i) {
        JsonObject field = fields.createNestedObject();
        field["name"] = color_fields[i]; field["type"] = "color"; field["bindable"] = true;
        field["default"] = color_defaults[i]; field["max_length"] = CONFIG_COLOR_MAX_LEN - 1;
    }
    for (const char* name : {"widget_progress_inactive_color"}) {
        JsonObject field = fields.createNestedObject();
        field["name"] = name; field["type"] = "color"; field["bindable"] = true;
        field["default"] = "auto"; field["max_length"] = CONFIG_COLOR_MAX_LEN - 1;
    }
    add_choice("widget_progress_layout", "grid", layouts);
    add_choice("widget_progress_shape", "circle", shapes);
    add_choice("widget_progress_inactive_style", "auto", inactive_styles);
    add_choice("widget_progress_partial", "size", partials);
    add_choice("widget_progress_order", "rows", orders);
    add_choice("widget_progress_origin", "top_left", origins);
    add_choice("widget_progress_fill_direction", "auto", directions);
    add_choice("widget_progress_effect", "grow", effects);
    numeric("widget_progress_aspect", 0.1, 10, 1);
    numeric("widget_progress_rounding", 0, 50, 0);
    numeric("widget_progress_inactive_size", 1, 100, 25);
    numeric("widget_progress_inactive_outline_width", 1, 10, 2);
    numeric("widget_anim_ms", 0, 5000, 300);
    numeric("widget_progress_stagger_ms", 0, 1000, 0);
    numeric("widget_progress_seed", 0, UINT32_MAX, 84721);
    for (const char* name : {"widget_progress_columns", "widget_progress_gap_x",
                             "widget_progress_gap_y", "widget_progress_thickness"}) {
        JsonObject field = fields.createNestedObject();
        field["name"] = name; field["type"] = "number"; field["default"] = "auto";
        const bool gap = !strcmp(name, "widget_progress_gap_x") || !strcmp(name, "widget_progress_gap_y");
        field["minimum"] = gap ? 0 : 1;
        field["maximum"] = gap ? 256 : !strcmp(name, "widget_progress_thickness") ? 100 : candidate_limit;
        field["description"] = "Omit or use auto for adaptive geometry.";
    }
    JsonObject invert = fields.createNestedObject();
    invert["name"] = "widget_progress_invert"; invert["type"] = "boolean"; invert["default"] = false;
    JsonObject match_spacing = fields.createNestedObject();
    match_spacing["name"] = "widget_progress_match_spacing"; match_spacing["type"] = "boolean";
    match_spacing["default"] = true;
    match_spacing["description"] = "When false and both gaps are auto, spread units independently along each axis.";
    for (const char* name : value_fields) {
        JsonObject current = fields.createNestedObject();
        current["name"] = name; current["type"] = "string";
        current["bindable"] = true; current["max_length"] = CONFIG_LABEL_MAX_LEN - 1;
        current["default"] = "";
        current["description"] = "Finite numeric value or binding; blank contributes zero. Additional sources enable stacking.";
    }
    out["note"] = "Passive Progress with up to four ordered series. Stacking requires nonnegative values, minimum zero, and no inversion. Overflow clips in series order; fractional boundaries share one shape. Count is independent of range. E-paper and disabled animations snap. Unavailable inputs and insufficient space are reported.";
}
#endif

const WidgetPreview preview{"Progress", "more_horiz"};
} // namespace progress_widget

static const WidgetType progress_widget_type{
    "progress", progress_widget::parse, progress_widget::create, nullptr,
    progress_widget::destroy, progress_widget::tick, nullptr, true,
#if HAS_MCP
    progress_widget::describe,
#else
    nullptr,
#endif
    progress_widget::show, progress_widget::hide, progress_widget::validate,
    &progress_widget::preview, nullptr, nullptr, nullptr, nullptr,
    progress_widget::visit, nullptr, nullptr, false
};
static struct ProgressRegistration {
    ProgressRegistration() { widget_register(&progress_widget_type); }
} progress_registration;

#endif
