#pragma once

#include <stdint.h>

namespace calculator {

constexpr int kColumns = 5;
constexpr int kRows = 5;
constexpr int kMinKeyWidth = 40;
constexpr int kMinKeyHeight = 36;
constexpr int kMinDisplayHeight = 36;
constexpr int kMinGap = 4;
constexpr float kLimit = 1.0e30f;
constexpr unsigned kEntryDigits = 7;

struct Layout {
    int width;
    int height;
    int gap;
    int display_height;
    int key_width;
    int key_height;
};

static inline bool layout(int width, int height, Layout* out) {
    if (!out || width <= 0 || height <= 0) return false;
    int gap = (width < height ? width : height) / 40;
    if (gap < kMinGap) gap = kMinGap;
    if (gap > 12) gap = 12;
    int display_height = height / 6;
    if (display_height < kMinDisplayHeight) display_height = kMinDisplayHeight;
    const int key_width = (width - (kColumns + 1) * gap) / kColumns;
    const int key_height = (height - display_height - (kRows + 2) * gap) / kRows;
    if (key_width < kMinKeyWidth || key_height < kMinKeyHeight) return false;
    *out = {width, height, gap, display_height, key_width, key_height};
    return true;
}

enum Key : uint8_t {
    MemoryClear, MemoryRecall, MemoryAdd, MemorySubtract, SquareRoot,
    Digit7, Digit8, Digit9, Divide, Percent,
    Digit4, Digit5, Digit6, Multiply, Clear,
    Digit1, Digit2, Digit3, Subtract, Sign,
    Digit0, Decimal, Equals, Add
};

enum Operation : uint8_t { None, Plus, Minus, Times, DividedBy };

struct State {
    float value;
    float left;
    float memory;
    float repeated_operand;
    Operation pending;
    Operation repeated;
    char entry[16];
    uint8_t entry_length;
    uint8_t entry_digits;
    bool entering;
    bool replace;
    bool operand_ready;
    bool error;
};

static inline void reset(State* state) {
    const float memory = state->memory;
    *state = {};
    state->memory = memory;
    state->replace = true;
}

static inline bool valid(float value) {
    return value == value && value <= kLimit && value >= -kLimit;
}

static inline bool apply(Operation op, float left, float right, float* result) {
    switch (op) {
        case Plus: *result = left + right; break;
        case Minus: *result = left - right; break;
        case Times: *result = left * right; break;
        case DividedBy:
            if (right == 0.0f) return false;
            *result = left / right;
            break;
        case None: *result = right; break;
    }
    return valid(*result);
}

static inline float entry_value(const State& state) {
    float value = 0.0f;
    float fraction = 0.1f;
    bool decimal = false;
    unsigned index = 0;
    const bool negative = state.entry[0] == '-';
    if (negative) index = 1;
    for (; index < state.entry_length; ++index) {
        const char ch = state.entry[index];
        if (ch == '.') decimal = true;
        else if (decimal) {
            value += static_cast<float>(ch - '0') * fraction;
            fraction *= 0.1f;
        } else value = value * 10.0f + static_cast<float>(ch - '0');
    }
    return negative ? -value : value;
}

static inline void begin_entry(State* state) {
    if (!state->entering || state->replace) {
        if (state->pending == None) state->repeated = None;
        state->entry[0] = '\0';
        state->entry_length = 0;
        state->entry_digits = 0;
        state->entering = true;
        state->replace = false;
    }
}

static inline void digit(State* state, char ch) {
    begin_entry(state);
    if (state->entry_digits >= kEntryDigits) return;
    if (state->entry_length == 1 && state->entry[0] == '0') {
        if (ch == '0') return;
        state->entry_length = 0;
        state->entry_digits = 0;
    }
    if (state->entry_length == 2 && state->entry[0] == '-' && state->entry[1] == '0') {
        if (ch == '0') return;
        state->entry_length = 1;
        state->entry_digits = 0;
    }
    state->entry[state->entry_length++] = ch;
    state->entry[state->entry_length] = '\0';
    ++state->entry_digits;
    state->value = entry_value(*state);
    state->operand_ready = true;
}

static inline void decimal(State* state) {
    begin_entry(state);
    for (unsigned index = 0; index < state->entry_length; ++index)
        if (state->entry[index] == '.') return;
    if (!state->entry_length) {
        state->entry[state->entry_length++] = '0';
        ++state->entry_digits;
    }
    state->entry[state->entry_length++] = '.';
    state->entry[state->entry_length] = '\0';
    state->value = entry_value(*state);
    state->operand_ready = true;
}

static inline void press(State* state, Key key, float (*square_root)(float)) {
    if (key == Clear) { reset(state); return; }
    if (state->error) return;
    switch (key) {
        case Digit0: digit(state, '0'); return;
        case Digit1: digit(state, '1'); return;
        case Digit2: digit(state, '2'); return;
        case Digit3: digit(state, '3'); return;
        case Digit4: digit(state, '4'); return;
        case Digit5: digit(state, '5'); return;
        case Digit6: digit(state, '6'); return;
        case Digit7: digit(state, '7'); return;
        case Digit8: digit(state, '8'); return;
        case Digit9: digit(state, '9'); return;
        case Decimal: decimal(state); return;
        case MemoryClear: state->memory = 0.0f; return;
        case MemoryRecall:
            state->value = state->memory;
            if (state->pending == None) state->repeated = None;
            state->entering = false;
            state->replace = true;
            state->operand_ready = true;
            return;
        case MemoryAdd:
        case MemorySubtract: {
            const float memory = state->memory + (key == MemoryAdd ? state->value : -state->value);
            if (!valid(memory)) state->error = true;
            else state->memory = memory;
            return;
        }
        case Sign:
            state->value = -state->value;
            if (state->entering && !state->replace) {
                if (state->entry[0] == '-') {
                    for (unsigned i = 0; i < state->entry_length; ++i) state->entry[i] = state->entry[i + 1];
                    --state->entry_length;
                } else {
                    for (unsigned i = state->entry_length + 1; i > 0; --i) state->entry[i] = state->entry[i - 1];
                    state->entry[0] = '-';
                    ++state->entry_length;
                }
            }
            state->operand_ready = true;
            return;
        case SquareRoot:
            if (state->value < 0.0f || !square_root) { state->error = true; return; }
            state->value = square_root(state->value);
            state->entering = false;
            state->replace = true;
            state->operand_ready = true;
            return;
        case Percent:
            if (state->pending == Plus || state->pending == Minus)
                state->value = (state->left / 100.0f) * state->value;
            else state->value /= 100.0f;
            if (!valid(state->value)) state->error = true;
            state->entering = false;
            state->replace = true;
            state->operand_ready = true;
            return;
        default: break;
    }

    if (key == Equals) {
        if (state->pending != None) {
            state->repeated = state->pending;
            state->repeated_operand = state->value;
            if (!apply(state->pending, state->left, state->value, &state->value)) state->error = true;
            state->pending = None;
        } else if (state->repeated != None) {
            if (!apply(state->repeated, state->value, state->repeated_operand, &state->value))
                state->error = true;
        }
        state->entering = false;
        state->replace = true;
        state->operand_ready = false;
        return;
    }

    const Operation operation = key == Add ? Plus : key == Subtract ? Minus :
        key == Multiply ? Times : key == Divide ? DividedBy : None;
    if (operation == None) return;
    if (state->pending != None && state->operand_ready) {
        if (!apply(state->pending, state->left, state->value, &state->value)) {
            state->error = true;
            return;
        }
    }
    state->left = state->value;
    state->pending = operation;
    state->repeated = None;
    state->entering = false;
    state->replace = true;
    state->operand_ready = false;
}

static inline void display(const State& state, char* out) {
    if (state.error) {
        const char* text = "Error";
        while ((*out++ = *text++)) {}
        return;
    }
    if (state.entering && !state.replace) {
        const char* text = state.entry;
        while ((*out++ = *text++)) {}
        return;
    }
    float value = state.value;
    if (value == 0.0f) { out[0] = '0'; out[1] = '\0'; return; }
    if (value < 0.0f) { *out++ = '-'; value = -value; }
    float magnitude = value;
    int exponent = 0;
    while (magnitude >= 10.0f) { magnitude /= 10.0f; ++exponent; }
    while (magnitude < 1.0f) { magnitude *= 10.0f; --exponent; }
    int scale = 6 - exponent;
    float correction = 0.0f;
    // Retain scaling errors so binary rounding does not invent a decimal digit.
    while (scale != 0) {
        if (scale > 0) {
            const float next = value * 10.0f;
            correction = __builtin_fmaf(value, 10.0f, -next) + correction * 10.0f;
            value = next;
            --scale;
        } else {
            const float next = value / 10.0f;
            correction = (__builtin_fmaf(-next, 10.0f, value) + correction) / 10.0f;
            value = next;
            ++scale;
        }
        const float adjusted = value + correction;
        correction += value - adjusted;
        value = adjusted;
    }
    uint32_t rounded = static_cast<uint32_t>(value);
    const float fraction = value - static_cast<float>(rounded);
    if (fraction > 0.5f || (fraction == 0.5f && correction >= 0.0f) ||
        (fraction < 0.5f && correction >= 0.5f - fraction)) ++rounded;
    if (rounded >= 10000000u) { rounded = 1000000u; ++exponent; }
    char digits[7];
    for (int i = 6; i >= 0; --i) { digits[i] = static_cast<char>('0' + rounded % 10); rounded /= 10; }
    int last = 6;
    while (last > 0 && digits[last] == '0') --last;
    if (exponent >= 7 || exponent <= -5) {
        *out++ = digits[0];
        if (last > 0) {
            *out++ = '.';
            for (int i = 1; i <= last; ++i) *out++ = digits[i];
        }
        *out++ = 'e';
        if (exponent < 0) { *out++ = '-'; exponent = -exponent; }
        else *out++ = '+';
        if (exponent >= 10) *out++ = static_cast<char>('0' + exponent / 10);
        *out++ = static_cast<char>('0' + exponent % 10);
    } else if (exponent < 0) {
        *out++ = '0'; *out++ = '.';
        for (int i = -1; i > exponent; --i) *out++ = '0';
        for (int i = 0; i <= last; ++i) *out++ = digits[i];
    } else {
        for (int i = 0; i <= exponent; ++i) *out++ = digits[i];
        if (last > exponent) {
            *out++ = '.';
            for (int i = exponent + 1; i <= last; ++i) *out++ = digits[i];
        }
    }
    *out = '\0';
}

} // namespace calculator
