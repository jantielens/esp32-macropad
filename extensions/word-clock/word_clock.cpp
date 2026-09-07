#include "native_extension_api.h"
#include "word_clock_phrase.h"

extern "C" const NativeExtensionDescriptor native_extension_descriptor = {
    NATIVE_EXTENSION_DESCRIPTOR_MAGIC, NATIVE_EXTENSION_ABI_VERSION,
    NATIVE_EXTENSION_TARGET_ABI, "word-clock", "1.0.3", "Word Clock", 50, 0,
};

namespace {

constexpr uint8_t MAX_INSTANCES = 16;
constexpr uint8_t GRID_ROWS = 10;
constexpr uint8_t GRID_COLUMNS = 11;
constexpr uint8_t ACCURATE_GRID_ROWS = 18;
constexpr uint8_t ACCURATE_GRID_COLUMNS = 18;
constexpr uint16_t MAX_FACE_CELLS = ACCURATE_GRID_ROWS * ACCURATE_GRID_COLUMNS;
constexpr uint16_t FACE_MASK_BYTES = (MAX_FACE_CELLS + 7u) / 8u;
constexpr uint8_t FONT_SIZES[] = {48, 36, 32, 24, 18, 14, 12};
constexpr uint8_t FONT_SIZE_COUNT = sizeof(FONT_SIZES) / sizeof(FONT_SIZES[0]);
constexpr uint8_t TIME_TEMPLATE_CAPACITY = 96;
constexpr uint32_t RESOLVE_INTERVAL_MS = 500;
constexpr uint32_t DEFAULT_PHRASE_ANIMATION_MS = 0;
constexpr uint32_t DEFAULT_FOREGROUND_RGB = 0xF4EFE1;
constexpr uint32_t DEFAULT_DIMMED_RGB = 0x383631;
constexpr uint32_t DEFAULT_SHIFT_MINUTES = 60;
constexpr uint32_t MAX_SHIFT_MINUTES = 30000;
constexpr uint8_t MAX_SHIFT_PIXELS = 24;
constexpr uint8_t DEFAULT_ACCURATE_PAST_THRESHOLD_MINUTES = 35;
constexpr uint8_t MIN_ACCURATE_PAST_THRESHOLD_MINUTES = 30;
constexpr uint8_t MAX_ACCURATE_PAST_THRESHOLD_MINUTES = 44;
constexpr uint8_t LOG_MESSAGE_CAPACITY = 192;
constexpr uint16_t OUTER_MARGIN = 6;
constexpr char GRID[GRID_ROWS][GRID_COLUMNS + 1] = {
    "ITLISASTIME",
    "ACQUARTERDC",
    "TWENTYFIVE#",
    "HALFBTENFTO",
    "PASTERUNINE",
    "ONESIXTHREE",
    "FOURFIVETWO",
    "EIGHTELEVEN",
    "SEVENTWELVE",
    "TENSEOCLOCK",
};
constexpr char ACCURATE_GRID[ACCURATE_GRID_ROWS][ACCURATE_GRID_COLUMNS + 1] = {
    "IT#IS#TWENTYTHIRTY", "FORTY#FIFTY#ONE###", "TWO#THREE#FOUR####", "FIVE#SIX#SEVEN####",
    "EIGHT#NINE#TEN####", "ELEVEN#TWELVE#####", "THIRTEEN#FIFTEEN##", "FOURTEEN#SIXTEEN##",
    "SEVENTEEN#########", "EIGHTEEN#NINETEEN#", "MINUTES#QUARTER###", "HALF#PAST#TO######",
    "ONE#TWO#THREE#####", "FOUR#FIVE#SIX#####", "SEVEN#EIGHT#######", "NINE#TEN#ELEVEN###",
    "TWELVE#OCLOCK#####", "##################",
};

struct WordRange { uint8_t row, column, length; };
constexpr WordRange WORDS[] = {
    {0, 0, 2}, {0, 3, 2}, {2, 6, 4}, {3, 5, 3}, {1, 2, 7}, {2, 0, 6},
    {3, 0, 4}, {4, 0, 4}, {3, 9, 2}, {5, 0, 3}, {6, 8, 3}, {5, 6, 5},
    {6, 0, 4}, {6, 4, 4}, {5, 3, 3}, {8, 0, 5}, {7, 0, 5}, {4, 7, 4},
    {9, 0, 3}, {7, 5, 6}, {8, 5, 6}, {9, 5, 6},
};
constexpr WordRange ACCURATE_WORDS[] = {
    {0, 0, 2}, {0, 3, 2}, {10, 0, 6}, {10, 6, 1}, {1, 12, 3}, {2, 0, 3},
    {2, 4, 5}, {2, 10, 4}, {3, 0, 4}, {3, 5, 3}, {3, 9, 5}, {4, 0, 5},
    {4, 6, 4}, {4, 11, 3}, {5, 0, 6}, {5, 7, 6}, {6, 0, 8}, {7, 0, 8},
    {6, 9, 7}, {7, 9, 7}, {8, 0, 9}, {9, 0, 8}, {9, 9, 8}, {0, 6, 6},
    {0, 12, 6}, {1, 0, 5}, {1, 6, 5}, {10, 8, 7}, {11, 0, 4}, {11, 5, 4},
    {11, 10, 2}, {12, 0, 3}, {12, 4, 3}, {12, 8, 5}, {13, 0, 4}, {13, 5, 4},
    {13, 10, 3}, {14, 0, 5}, {14, 6, 5}, {15, 0, 4}, {15, 5, 3}, {15, 9, 6},
    {16, 0, 6}, {16, 7, 6},
};
static_assert(sizeof(ACCURATE_WORDS) / sizeof(ACCURATE_WORDS[0]) == WORD_CLOCK_ACCURATE_OCLOCK + 1u,
              "accurate word-clock face must map every phrase word");
static_assert(ACCURATE_GRID[7][11] == 'X' && GRID[5][5] == 'X',
              "SIX must retain its literal X rather than use a filler marker");
static_assert(GRID[1][WORDS[4].column] == 'Q' && GRID[1][WORDS[4].column + 6] == 'R',
              "QUARTER range must cover QUARTER in the word-clock face");
constexpr const char* ACCURATE_WORD_TEXTS[] = {
    "IT", "IS", "MINUTE", "S", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT",
    "NINE", "TEN", "ELEVEN", "TWELVE", "THIRTEEN", "FOURTEEN", "FIFTEEN", "SIXTEEN", "SEVENTEEN",
    "EIGHTEEN", "NINETEEN", "TWENTY", "THIRTY", "FORTY", "FIFTY", "QUARTER", "HALF", "PAST", "TO",
    "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN",
    "TWELVE", "OCLOCK",
};
static_assert(sizeof(ACCURATE_WORD_TEXTS) / sizeof(ACCURATE_WORD_TEXTS[0]) ==
                  sizeof(ACCURATE_WORDS) / sizeof(ACCURATE_WORDS[0]),
              "accurate word-clock text must map every phrase word");

constexpr bool accurate_range_matches_text(const WordRange& range, const char* text) {
    for (uint8_t offset = 0; offset < range.length; ++offset)
        if (ACCURATE_GRID[range.row][range.column + offset] != text[offset]) return false;
    return text[range.length] == '\0';
}

constexpr bool accurate_ranges_match_face() {
    for (uint8_t word = 0; word < sizeof(ACCURATE_WORDS) / sizeof(ACCURATE_WORDS[0]); ++word)
        if (!accurate_range_matches_text(ACCURATE_WORDS[word], ACCURATE_WORD_TEXTS[word])) return false;
    return true;
}

static_assert(accurate_ranges_match_face(),
              "every accurate word range must exactly match its text in the word-clock face");
static_assert(ACCURATE_WORDS[WORD_CLOCK_ACCURATE_OCLOCK].row >=
                  ACCURATE_WORDS[WORD_CLOCK_ACCURATE_HOUR_TWELVE].row,
              "OCLOCK must follow the hour words in the accurate face");
constexpr char WORD_NAMES[][11] = {
    "IT", "IS", "FIVE_MIN", "TEN_MIN", "QUARTER", "TWENTY", "HALF",
    "PAST", "TO", "ONE", "TWO", "THREE", "FOUR", "FIVE_HOUR", "SIX",
    "SEVEN", "EIGHT", "NINE", "TEN_HOUR", "ELEVEN", "TWELVE", "OCLOCK",
};
constexpr char ACCURATE_WORD_NAMES[][14] = {
    "IT", "IS", "MINUTE", "MINUTE_PLURAL", "MIN_ONE", "MIN_TWO", "MIN_THREE",
    "MIN_FOUR", "MIN_FIVE", "MIN_SIX", "MIN_SEVEN", "MIN_EIGHT", "MIN_NINE",
    "MIN_TEN", "MIN_ELEVEN", "MIN_TWELVE", "THIRTEEN", "FOURTEEN", "FIFTEEN",
    "SIXTEEN", "SEVENTEEN", "EIGHTEEN", "NINETEEN", "TWENTY", "THIRTY", "FORTY",
    "FIFTY", "QUARTER", "HALF", "PAST", "TO", "HOUR_ONE", "HOUR_TWO", "HOUR_THREE",
    "HOUR_FOUR", "HOUR_FIVE", "HOUR_SIX", "HOUR_SEVEN", "HOUR_EIGHT", "HOUR_NINE",
    "HOUR_TEN", "HOUR_ELEVEN", "HOUR_TWELVE", "OCLOCK",
};
static_assert(sizeof(ACCURATE_WORD_NAMES) / sizeof(ACCURATE_WORD_NAMES[0]) == WORD_CLOCK_ACCURATE_OCLOCK + 1u,
              "accurate word-clock names must match the phrase vocabulary");

struct InstanceState {
    bool active;
    bool dirty;
    uint32_t instance_id;
    void* extension_context;
    void* canvas;
    void* buffer;
    uint16_t width;
    uint16_t height;
    uint16_t cell_width;
    uint16_t cell_height;
    uint16_t dot_radius;
    uint8_t glyph_widths[26];
    int16_t origin_x;
    int16_t origin_y;
    int16_t shift_x;
    int16_t shift_y;
    uint8_t font_size;
    char font_name[16];
    char time_template[TIME_TEMPLATE_CAPACITY];
    char last_time[5];
    char previous_time[5];
    uint32_t foreground_rgb;
    uint32_t dimmed_rgb;
    uint32_t background_rgb;
    uint32_t next_resolve_ms;
    uint32_t next_animation_ms;
    uint32_t last_shift_ms;
    uint32_t shift_interval_ms;
    uint8_t shift_pixels;
    uint8_t shift_phase;
    uint8_t accurate_past_threshold_minutes;
    uint16_t animation_progress;
    uint16_t animation_removed_count;
    uint16_t animation_added_count;
    uint32_t phrase_animation_ms;
    bool animation_active;
    uint8_t previous_active_cells[FACE_MASK_BYTES];
    uint8_t current_active_cells[FACE_MASK_BYTES];
    WordClockMode mode;
};

struct PackageState { InstanceState instances[MAX_INSTANCES]; };

void copy_text(char* out, size_t capacity, const char* value) {
    if (!out || capacity == 0) return;
    size_t index = 0;
    while (value && value[index] && index + 1 < capacity) { out[index] = value[index]; ++index; }
    out[index] = '\0';
}

char* append_text(char* out, const char* end, const char* value) {
    while (value && *value && out + 1 < end) *out++ = *value++;
    return out;
}

uint8_t face_rows(WordClockMode mode) {
    return mode == WORD_CLOCK_MODE_ACCURATE ? ACCURATE_GRID_ROWS : GRID_ROWS;
}

uint8_t face_columns(WordClockMode mode) {
    return mode == WORD_CLOCK_MODE_ACCURATE ? ACCURATE_GRID_COLUMNS : GRID_COLUMNS;
}

char face_letter(WordClockMode mode, uint8_t row, uint8_t column) {
    const char letter = mode == WORD_CLOCK_MODE_ACCURATE ? ACCURATE_GRID[row][column] : GRID[row][column];
    if (letter != '#') return letter;
    return static_cast<char>('A' + (row * 17u + column * 11u + static_cast<uint8_t>(mode) * 7u) % 26u);
}

void log_phrase(const NativeExtensionHostApi* host, const InstanceState* instance, const char* resolved) {
    if (!host || !host->core || !host->core->log || !instance) return;
    char message[LOG_MESSAGE_CAPACITY] = "word clock time=";
    char* cursor = message + sizeof("word clock time=") - 1;
    const char* end = message + sizeof(message);
    cursor = append_text(cursor, end, instance->last_time);
    cursor = append_text(cursor, end, " resolved=");
    cursor = append_text(cursor, end, resolved ? resolved : "");
    cursor = append_text(cursor, end, " mode=");
    cursor = append_text(cursor, end,
                         instance->mode == WORD_CLOCK_MODE_MINUTE_DOTS ? "minute-dots" :
                         (instance->mode == WORD_CLOCK_MODE_ACCURATE ? "accurate" : "rounded"));
    cursor = append_text(cursor, end, " words=");
    const uint8_t hour = static_cast<uint8_t>((instance->last_time[0] - '0') * 10u + instance->last_time[1] - '0');
    const uint8_t minute = static_cast<uint8_t>((instance->last_time[2] - '0') * 10u + instance->last_time[3] - '0');
    bool first = true;
    if (instance->mode == WORD_CLOCK_MODE_ACCURATE) {
        for (uint8_t word = 0; word < sizeof(ACCURATE_WORDS) / sizeof(ACCURATE_WORDS[0]); ++word) {
            if (!word_clock_accurate_word_is_active(static_cast<WordClockAccurateWord>(word), hour, minute,
                                                    instance->accurate_past_threshold_minutes)) continue;
            if (!first) cursor = append_text(cursor, end, ",");
            cursor = append_text(cursor, end, ACCURATE_WORD_NAMES[word]);
            first = false;
        }
    } else {
        for (uint8_t word = 0; word < sizeof(WORDS) / sizeof(WORDS[0]); ++word) {
            if (!word_clock_word_is_active(static_cast<WordClockWord>(word), hour, minute, instance->mode)) continue;
            if (!first) cursor = append_text(cursor, end, ",");
            cursor = append_text(cursor, end, WORD_NAMES[word]);
            first = false;
        }
    }
    if (instance->mode == WORD_CLOCK_MODE_MINUTE_DOTS) {
        cursor = append_text(cursor, end, " dots=");
        *cursor++ = static_cast<char>('0' + word_clock_minute_dots(minute, instance->mode));
    }
    *cursor = '\0';
    host->core->log(NATIVE_EXTENSION_LOG_INFO, message);
}

bool text_equals(const char* left, const char* right) {
    size_t index = 0;
    while (left[index] && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

bool is_json_whitespace(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

bool find_number(const char* json, const char* key, uint32_t* out) {
    if (!json || !key || !out) return false;
    for (const char* cursor = json; *cursor; ++cursor) {
        if (*cursor != '"') continue;
        const char* name = cursor + 1;
        const char* key_cursor = key;
        while (*name && *key_cursor && *name == *key_cursor) { ++name; ++key_cursor; }
        if (*key_cursor || *name != '"') continue;
        while (*name && *name != ':') ++name;
        if (*name++ != ':') return false;
        while (is_json_whitespace(*name)) ++name;
        uint32_t value = 0;
        bool found = false;
        while (*name >= '0' && *name <= '9') {
            value = value * 10u + static_cast<uint32_t>(*name++ - '0');
            found = true;
        }
        if (!found || (*name && !is_json_whitespace(*name) && *name != ',' && *name != '}')) return false;
        *out = value;
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
        if (*name++ != ':') return false;
        while (*name == ' ' || *name == '\t') ++name;
        if (*name++ != '"') return false;
        size_t length = 0;
        while (*name && *name != '"' && length + 1 < capacity) out[length++] = *name++;
        out[length] = '\0';
        return *name == '"';
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

void parse_color(const char* json, const char* key, uint32_t* color) {
    char value[16] = {};
    if (find_string(json, key, value, sizeof(value))) parse_hex_color(value, color);
}

WordClockMode parse_mode(const char* json) {
    char value[16] = {};
    if (find_string(json, "mode", value, sizeof(value))) {
        if (text_equals(value, "minute-dots")) return WORD_CLOCK_MODE_MINUTE_DOTS;
        if (text_equals(value, "accurate")) return WORD_CLOCK_MODE_ACCURATE;
    }
    return WORD_CLOCK_MODE_ROUNDED;
}

uint32_t parse_shift_interval(const char* json) {
    uint32_t minutes = 0;
    if (!find_number(json, "burn_in_shift_minutes", &minutes)) return DEFAULT_SHIFT_MINUTES * 60000u;
    if (minutes > MAX_SHIFT_MINUTES) minutes = MAX_SHIFT_MINUTES;
    return minutes * 60000u;
}

uint8_t parse_shift_pixels(const char* json) {
    uint32_t pixels = 0;
    if (find_number(json, "burn_in_shift_pixels", &pixels) && pixels <= MAX_SHIFT_PIXELS)
        return static_cast<uint8_t>(pixels);
    return 0;
}

uint8_t parse_accurate_past_threshold(const char* json) {
    uint32_t threshold = DEFAULT_ACCURATE_PAST_THRESHOLD_MINUTES;
    if (find_number(json, "accurate_past_threshold_minutes", &threshold)) {
        if (threshold < MIN_ACCURATE_PAST_THRESHOLD_MINUTES) threshold = MIN_ACCURATE_PAST_THRESHOLD_MINUTES;
        if (threshold > MAX_ACCURATE_PAST_THRESHOLD_MINUTES) threshold = MAX_ACCURATE_PAST_THRESHOLD_MINUTES;
    }
    return static_cast<uint8_t>(threshold);
}

uint32_t parse_phrase_animation_interval(const char* json) {
    uint32_t interval = DEFAULT_PHRASE_ANIMATION_MS;
    find_number(json, "phrase_animation_ms", &interval);
    return interval;
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
        return &instance;
    }
    return nullptr;
}

uint8_t nearest_font_size(uint32_t requested) {
    uint8_t nearest = FONT_SIZES[FONT_SIZE_COUNT - 1];
    uint32_t nearest_distance = 255;
    for (uint8_t index = 0; index < FONT_SIZE_COUNT; ++index) {
        const uint32_t candidate = FONT_SIZES[index];
        const uint32_t distance = candidate > requested ? candidate - requested : requested - candidate;
        if (distance < nearest_distance) { nearest = static_cast<uint8_t>(candidate); nearest_distance = distance; }
    }
    return nearest;
}

bool build_layout(const NativeExtensionHostApi* host, InstanceState* instance, uint32_t requested_size) {
    const uint16_t dot_gutter = instance->mode == WORD_CLOCK_MODE_MINUTE_DOTS
        ? static_cast<uint16_t>((instance->width < instance->height ? instance->width : instance->height) / 14u + 4u)
        : 0;
    int32_t face_width = instance->width - 2 * (OUTER_MARGIN + dot_gutter);
    int32_t face_height = instance->height - 2 * (OUTER_MARGIN + dot_gutter);
    if (face_width <= 0 || face_height <= 0) return false;
    const uint8_t rows = face_rows(instance->mode);
    const uint8_t columns = face_columns(instance->mode);
    if (face_width * rows > face_height * columns) face_width = face_height * columns / rows;
    else face_height = face_width * rows / columns;
    const uint16_t cell_width = static_cast<uint16_t>(face_width / columns);
    const uint16_t cell_height = static_cast<uint16_t>(face_height / rows);
    if (!cell_width || !cell_height) return false;
    const uint8_t maximum_size = requested_size ? nearest_font_size(requested_size) : FONT_SIZES[0];
    for (uint8_t index = 0; index < FONT_SIZE_COUNT; ++index) {
        const uint8_t candidate = FONT_SIZES[index];
        if (candidate > maximum_size) continue;
        const int32_t widest = host->canvas->canvas_measure_text("W", instance->font_name, candidate);
        if (widest > 0 && widest <= cell_width && candidate <= cell_height) {
            instance->font_size = candidate;
            instance->cell_width = cell_width;
            instance->cell_height = cell_height;
            for (uint8_t index = 0; index < 26; ++index) {
                char letter[2] = {static_cast<char>('A' + index), '\0'};
                const int32_t width = host->canvas->canvas_measure_text(letter, instance->font_name, candidate);
                instance->glyph_widths[index] = static_cast<uint8_t>(width > 255 ? 255 : (width < 0 ? 0 : width));
            }
            instance->origin_x = static_cast<int16_t>((instance->width - cell_width * columns) / 2);
            instance->origin_y = static_cast<int16_t>((instance->height - cell_height * rows) / 2);
            instance->dot_radius = instance->mode == WORD_CLOCK_MODE_MINUTE_DOTS
                ? static_cast<uint16_t>((cell_width < cell_height ? cell_width : cell_height) / 9u) : 0;
            if (instance->dot_radius < 2 && instance->mode == WORD_CLOCK_MODE_MINUTE_DOTS) instance->dot_radius = 2;
            return true;
        }
    }
    return false;
}

bool normalize_time(const char* source, char* out) {
    char digits[4] = {};
    uint8_t count = 0;
    for (; source && *source && count < sizeof(digits); ++source)
        if (*source >= '0' && *source <= '9') digits[count++] = *source;
    if (count != sizeof(digits)) return false;
    const uint8_t hour = static_cast<uint8_t>((digits[0] - '0') * 10u + digits[1] - '0');
    const uint8_t minute = static_cast<uint8_t>((digits[2] - '0') * 10u + digits[3] - '0');
    if (hour >= 24 || minute >= 60) return false;
    for (uint8_t index = 0; index < sizeof(digits); ++index) out[index] = digits[index];
    out[4] = '\0';
    return true;
}

void draw_letter(const NativeExtensionHostApi* host, const InstanceState* instance,
                 uint8_t row, uint8_t column, uint32_t color) {
    char letter[2] = {face_letter(instance->mode, row, column), '\0'};
    const int32_t glyph_width = instance->glyph_widths[static_cast<uint8_t>(letter[0] - 'A')];
    const int32_t x = instance->origin_x + instance->shift_x + column * instance->cell_width +
                      (instance->cell_width - glyph_width) / 2;
    const int32_t y = instance->origin_y + instance->shift_y + row * instance->cell_height;
    host->canvas->canvas_draw_text(instance->canvas, x, y, letter, instance->font_name, instance->font_size, color);
}

bool cell_is_active(const InstanceState* instance, uint8_t row, uint8_t column,
                    uint8_t hour, uint8_t minute) {
    if (instance->mode == WORD_CLOCK_MODE_ACCURATE) {
        for (uint8_t word = 0; word < sizeof(ACCURATE_WORDS) / sizeof(ACCURATE_WORDS[0]); ++word) {
            const WordRange& range = ACCURATE_WORDS[word];
            if (range.row == row && column >= range.column && column < range.column + range.length &&
                word_clock_accurate_word_is_active(static_cast<WordClockAccurateWord>(word), hour, minute,
                                                   instance->accurate_past_threshold_minutes)) return true;
        }
    } else {
        for (uint8_t word = 0; word < sizeof(WORDS) / sizeof(WORDS[0]); ++word) {
            const WordRange& range = WORDS[word];
            if (range.row == row && column >= range.column && column < range.column + range.length &&
                word_clock_word_is_active(static_cast<WordClockWord>(word), hour, minute, instance->mode)) return true;
        }
    }
    return false;
}

bool time_cell_is_active(const InstanceState* instance, const char* time, uint8_t row, uint8_t column) {
    const uint8_t hour = static_cast<uint8_t>((time[0] - '0') * 10u + time[1] - '0');
    const uint8_t minute = static_cast<uint8_t>((time[2] - '0') * 10u + time[3] - '0');
    return cell_is_active(instance, row, column, hour, minute);
}

uint16_t cell_index(uint8_t row, uint8_t column) {
    return static_cast<uint16_t>(row) * ACCURATE_GRID_COLUMNS + column;
}

bool transition_mask_cell_is_active(const uint8_t* mask, uint8_t row, uint8_t column) {
    const uint16_t index = cell_index(row, column);
    return (mask[index / 8u] & (1u << (index % 8u))) != 0;
}

void transition_mask_set_cell(uint8_t* mask, uint8_t row, uint8_t column, bool active) {
    const uint16_t index = cell_index(row, column);
    const uint8_t bit = static_cast<uint8_t>(1u << (index % 8u));
    if (active) mask[index / 8u] |= bit;
    else mask[index / 8u] &= static_cast<uint8_t>(~bit);
}

void prepare_transition(InstanceState* instance) {
    for (uint16_t index = 0; index < FACE_MASK_BYTES; ++index) {
        instance->previous_active_cells[index] = 0;
        instance->current_active_cells[index] = 0;
    }
    uint16_t removed = 0;
    uint16_t added = 0;
    for (uint8_t row = 0; row < face_rows(instance->mode); ++row)
        for (uint8_t column = 0; column < face_columns(instance->mode); ++column) {
            const bool previous = time_cell_is_active(instance, instance->previous_time, row, column);
            const bool current = time_cell_is_active(instance, instance->last_time, row, column);
            transition_mask_set_cell(instance->previous_active_cells, row, column, previous);
            transition_mask_set_cell(instance->current_active_cells, row, column, current);
            if (previous && !current) ++removed;
            if (!previous && current) ++added;
        }
    const uint8_t previous_dots = word_clock_minute_dots(
        static_cast<uint8_t>((instance->previous_time[2] - '0') * 10u + instance->previous_time[3] - '0'), instance->mode);
    const uint8_t current_dots = word_clock_minute_dots(
        static_cast<uint8_t>((instance->last_time[2] - '0') * 10u + instance->last_time[3] - '0'), instance->mode);
    instance->animation_removed_count = static_cast<uint16_t>(removed +
        (previous_dots > current_dots ? previous_dots - current_dots : 0));
    instance->animation_added_count = static_cast<uint16_t>(added +
        (current_dots > previous_dots ? current_dots - previous_dots : 0));
}

bool transition_cell_is_active(const InstanceState* instance, uint8_t row, uint8_t column) {
    const bool previous = transition_mask_cell_is_active(instance->previous_active_cells, row, column);
    const bool current = transition_mask_cell_is_active(instance->current_active_cells, row, column);
    if (previous == current) return current;
    const uint16_t target_index = cell_index(row, column);
    uint16_t position = 0;
    if (previous) {
        for (uint8_t scan_row = 0; scan_row < face_rows(instance->mode); ++scan_row)
            for (uint8_t scan_column = 0; scan_column < face_columns(instance->mode); ++scan_column)
                if (transition_mask_cell_is_active(instance->previous_active_cells, static_cast<uint8_t>(scan_row),
                                                   static_cast<uint8_t>(scan_column)) &&
                    !transition_mask_cell_is_active(instance->current_active_cells, static_cast<uint8_t>(scan_row),
                                                    static_cast<uint8_t>(scan_column)) &&
                    cell_index(scan_row, scan_column) > target_index) ++position;
        return instance->animation_progress <= position;
    }
    for (uint8_t scan_row = 0; scan_row < face_rows(instance->mode); ++scan_row)
        for (uint8_t scan_column = 0; scan_column < face_columns(instance->mode); ++scan_column)
            if (!transition_mask_cell_is_active(instance->previous_active_cells, scan_row, scan_column) &&
                transition_mask_cell_is_active(instance->current_active_cells, scan_row, scan_column) &&
                cell_index(scan_row, scan_column) < target_index) ++position;
    return instance->animation_progress > instance->animation_removed_count + position;
}

void draw_filled_dot(const NativeExtensionHostApi* host, const InstanceState* instance,
                     int32_t center_x, int32_t center_y) {
    const int32_t radius = instance->dot_radius;
    for (int32_t y = -radius; y <= radius; ++y)
        for (int32_t x = -radius; x <= radius; ++x)
            if (x * x + y * y <= radius * radius)
                host->canvas->canvas_set_pixel(instance->canvas, center_x + x, center_y + y,
                                                instance->foreground_rgb);
}

void draw_clock(const NativeExtensionHostApi* host, const InstanceState* instance) {
    host->canvas->canvas_clear(instance->canvas, instance->background_rgb);
    const uint8_t rows = face_rows(instance->mode);
    const uint8_t columns = face_columns(instance->mode);
    for (uint8_t row = 0; row < rows; ++row)
        for (uint8_t column = 0; column < columns; ++column)
            draw_letter(host, instance, row, column,
                        (instance->animation_active
                            ? transition_cell_is_active(instance, row, column)
                            : time_cell_is_active(instance, instance->last_time, row, column))
                            ? instance->foreground_rgb : instance->dimmed_rgb);
    const uint8_t current_dots = word_clock_minute_dots(
        static_cast<uint8_t>((instance->last_time[2] - '0') * 10u + instance->last_time[3] - '0'), instance->mode);
    const uint8_t previous_dots = instance->animation_active ? word_clock_minute_dots(
        static_cast<uint8_t>((instance->previous_time[2] - '0') * 10u + instance->previous_time[3] - '0'), instance->mode) : 0;
    uint8_t dots = current_dots;
    if (instance->animation_active) {
        if (previous_dots > current_dots) {
            const uint8_t removed_dots = previous_dots - current_dots;
            const uint16_t character_count = instance->animation_removed_count - removed_dots;
            const uint16_t progress = instance->animation_progress > character_count
                ? instance->animation_progress - character_count : 0;
            const uint8_t completed = progress < removed_dots ? static_cast<uint8_t>(progress) : removed_dots;
            dots = static_cast<uint8_t>(previous_dots - (completed < removed_dots ? completed : removed_dots));
        } else if (current_dots > previous_dots) {
            const uint8_t added_dots = current_dots - previous_dots;
            const uint16_t character_count = instance->animation_added_count - added_dots;
            const uint16_t start = instance->animation_removed_count + character_count;
            const uint16_t progress = instance->animation_progress > start ? instance->animation_progress - start : 0;
            dots = static_cast<uint8_t>(previous_dots + (progress < added_dots ? progress : added_dots));
        }
    }
    const int32_t inset = instance->dot_radius + 2;
    const int32_t left = instance->origin_x - inset;
    const int32_t right = instance->origin_x + columns * instance->cell_width + inset - 1;
    const int32_t top = instance->origin_y - inset;
    const int32_t bottom = instance->origin_y + rows * instance->cell_height + inset - 1;
    const int32_t x[] = {left, right, right, left};
    const int32_t y[] = {top, top, bottom, bottom};
    for (uint8_t index = 0; index < dots; ++index)
        draw_filled_dot(host, instance, x[index], y[index]);
}

}  // namespace

extern "C" bool native_extension_create_instance(const NativeExtensionHostApi* host, void* extension_context,
                                                  uint32_t instance_id, void* root, const char* config_json) {
    if (!host || !host->core || !host->ui || !host->canvas || !host->binding || !root) return false;
    PackageState* state = package_state(host, extension_context);
    InstanceState* instance = create_instance(state, instance_id);
    if (!instance) return false;
    instance->extension_context = extension_context;
    instance->width = static_cast<uint16_t>(host->ui->obj_get_width(root));
    instance->height = static_cast<uint16_t>(host->ui->obj_get_height(root));
    instance->background_rgb = 0x000000;
    instance->foreground_rgb = DEFAULT_FOREGROUND_RGB;
    instance->dimmed_rgb = DEFAULT_DIMMED_RGB;
    copy_text(instance->font_name, sizeof(instance->font_name), "bebas");
    copy_text(instance->time_template, sizeof(instance->time_template), "[time:%H%M]");
    find_string(config_json, "font_family", instance->font_name, sizeof(instance->font_name));
    find_string(config_json, "time", instance->time_template, sizeof(instance->time_template));
    instance->mode = parse_mode(config_json);
    instance->accurate_past_threshold_minutes = parse_accurate_past_threshold(config_json);
    instance->phrase_animation_ms = parse_phrase_animation_interval(config_json);
    instance->shift_pixels = parse_shift_pixels(config_json);
    parse_color(config_json, "dimmed_color", &instance->dimmed_rgb);
    NativeExtensionButtonSnapshot button = {};
    if (host->button && host->button->get(extension_context, instance_id, &button)) {
        instance->background_rgb = button.background_rgb;
        instance->foreground_rgb = button.foreground_rgb;
    }
    uint32_t requested_size = 0;
    find_number(config_json, "font_size", &requested_size);
    if (!build_layout(host, instance, requested_size)) { instance->active = false; return false; }
    instance->canvas = host->canvas->canvas_create(root);
    instance->buffer = host->core->alloc(host->canvas->canvas_buffer_size(instance->width, instance->height));
    if (!instance->canvas || !instance->buffer) {
        if (instance->buffer) host->core->free(instance->buffer);
        if (instance->canvas && host->ui->obj_delete) host->ui->obj_delete(instance->canvas);
        *instance = {};
        return false;
    }
    if (!host->canvas->canvas_set_buffer(instance->canvas, instance->buffer, instance->width, instance->height)) {
        host->core->free(instance->buffer);
        if (host->ui->obj_delete) host->ui->obj_delete(instance->canvas);
        *instance = {};
        return false;
    }
    char resolved[TIME_TEMPLATE_CAPACITY] = {};
    if (!host->binding->resolve(extension_context, instance_id, instance->time_template, resolved, sizeof(resolved)) ||
        !normalize_time(resolved, instance->last_time)) copy_text(instance->last_time, sizeof(instance->last_time), "0000");
    log_phrase(host, instance, resolved);
    const uint32_t now = host->core->millis();
    instance->next_resolve_ms = now + RESOLVE_INTERVAL_MS;
    instance->last_shift_ms = now;
    instance->shift_interval_ms = parse_shift_interval(config_json);
    instance->shift_pixels = word_clock_burn_in_shift_pixels(instance->font_size, instance->shift_pixels);
    instance->dirty = true;
    return true;
}

extern "C" void native_extension_destroy_instance(const NativeExtensionHostApi* host, void* extension_context,
                                                   uint32_t instance_id) {
    if (!host || !host->core) return;
    PackageState* state = static_cast<PackageState*>(host->core->context_get_data(extension_context));
    InstanceState* instance = find_instance(state, instance_id);
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

extern "C" void native_extension_tick(const NativeExtensionHostApi* host, void* extension_context,
                                       uint32_t instance_id) {
    if (!host || !host->core || !host->canvas || !host->binding) return;
    PackageState* state = static_cast<PackageState*>(host->core->context_get_data(extension_context));
    InstanceState* instance = find_instance(state, instance_id);
    if (!instance || !instance->active || !instance->canvas) return;
    const uint32_t now = host->core->millis();
    if (now >= instance->next_resolve_ms) {
        char resolved[TIME_TEMPLATE_CAPACITY] = {};
        char time[sizeof(instance->last_time)] = {};
        if (host->binding->resolve(extension_context, instance_id, instance->time_template, resolved, sizeof(resolved)) &&
            normalize_time(resolved, time) && !text_equals(time, instance->last_time)) {
            copy_text(instance->previous_time, sizeof(instance->previous_time), instance->last_time);
            copy_text(instance->last_time, sizeof(instance->last_time), time);
            prepare_transition(instance);
            instance->animation_progress = 0;
            instance->animation_active = instance->phrase_animation_ms != 0;
            instance->next_animation_ms = now + instance->phrase_animation_ms;
            instance->dirty = true;
            log_phrase(host, instance, resolved);
        }
        instance->next_resolve_ms = now + RESOLVE_INTERVAL_MS;
    }
    if (instance->animation_active && now >= instance->next_animation_ms) {
        ++instance->animation_progress;
        instance->next_animation_ms = now + instance->phrase_animation_ms;
        if (instance->animation_progress >= instance->animation_removed_count + instance->animation_added_count)
            instance->animation_active = false;
        instance->dirty = true;
    }
    if (instance->shift_interval_ms && now - instance->last_shift_ms >= instance->shift_interval_ms) {
        instance->shift_phase = word_clock_burn_in_next_phase(instance->shift_phase);
        instance->shift_x = word_clock_burn_in_shift_x(instance->shift_phase, instance->shift_pixels);
        instance->shift_y = word_clock_burn_in_shift_y(instance->shift_phase, instance->shift_pixels);
        instance->last_shift_ms = now;
        instance->dirty = true;
    }
    if (!instance->dirty) return;
    draw_clock(host, instance);
    instance->dirty = false;
}