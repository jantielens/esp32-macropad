#pragma once

#include <cmath>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

constexpr int64_t SLIDER_FIXED_SCALE = 10000;
constexpr uint16_t SLIDER_MAX_INTERVALS = 10000;

struct SliderNumericRange {
    int64_t minimum;
    int64_t maximum;
    int64_t step;
    uint16_t intervals;
};

inline bool slider_fixed_from_double(double value, int64_t* output,
                                     double maximum_magnitude = 1000000.0) {
    if (!output || !std::isfinite(value) || std::fabs(value) > maximum_magnitude) return false;
    const double scaled = value * static_cast<double>(SLIDER_FIXED_SCALE);
    const double rounded = std::round(scaled);
    const double tolerance = std::numeric_limits<double>::epsilon()
                           * std::fmax(1.0, std::fabs(scaled)) * 4.0;
    if (std::fabs(scaled - rounded) > tolerance) return false;
    *output = static_cast<int64_t>(rounded);
    return true;
}

inline bool slider_numeric_range_init(double minimum, double maximum, double step,
                                      SliderNumericRange* output) {
    if (!output) return false;
    SliderNumericRange range{};
    if (!slider_fixed_from_double(minimum, &range.minimum)
            || !slider_fixed_from_double(maximum, &range.maximum)
            || !slider_fixed_from_double(step, &range.step, 2000000.0)
            || range.maximum <= range.minimum || range.step <= 0) return false;
    const int64_t span = range.maximum - range.minimum;
    if (span % range.step != 0) return false;
    const int64_t intervals = span / range.step;
    if (intervals < 1 || intervals > SLIDER_MAX_INTERVALS) return false;
    range.intervals = static_cast<uint16_t>(intervals);
    *output = range;
    return true;
}

inline int64_t slider_numeric_value_at(const SliderNumericRange& range, uint16_t position) {
    if (position > range.intervals) position = range.intervals;
    return range.minimum + static_cast<int64_t>(position) * range.step;
}

inline uint16_t slider_numeric_position_for(const SliderNumericRange& range, int64_t value) {
    if (value <= range.minimum) return 0;
    if (value >= range.maximum) return range.intervals;
    const int64_t offset = value - range.minimum;
    int64_t position = offset / range.step;
    const int64_t remainder = offset % range.step;
    if (remainder * 2 >= range.step) ++position;
    return static_cast<uint16_t>(position);
}

inline bool slider_numeric_decimal_syntax(const char* text) {
    const char* cursor = text;
    if (*cursor == '+' || *cursor == '-') ++cursor;
    bool digit = false;
    while (*cursor >= '0' && *cursor <= '9') { digit = true; ++cursor; }
    if (*cursor == '.') {
        ++cursor;
        while (*cursor >= '0' && *cursor <= '9') { digit = true; ++cursor; }
    }
    if (!digit) return false;
    if (*cursor == 'e' || *cursor == 'E') {
        ++cursor;
        if (*cursor == '+' || *cursor == '-') ++cursor;
        const char* exponent = cursor;
        while (*cursor >= '0' && *cursor <= '9') ++cursor;
        if (cursor == exponent) return false;
    }
    return *cursor == '\0';
}

inline bool slider_numeric_parse_source(const char* text, const SliderNumericRange& range,
                                        int64_t* output) {
    if (!text || !text[0] || !output || std::isspace(static_cast<unsigned char>(text[0])))
        return false;
    if (!slider_numeric_decimal_syntax(text)) return false;
    errno = 0;
    char* end = nullptr;
    const double source = std::strtod(text, &end);
    if (end == text || *end || errno == ERANGE || !std::isfinite(source)) return false;
    int64_t fixed = 0;
    if (source <= static_cast<double>(range.minimum) / SLIDER_FIXED_SCALE) {
        fixed = range.minimum;
    } else if (source >= static_cast<double>(range.maximum) / SLIDER_FIXED_SCALE) {
        fixed = range.maximum;
    } else if (!slider_fixed_from_double(source, &fixed)) {
        return false;
    }
    *output = slider_numeric_value_at(range, slider_numeric_position_for(range, fixed));
    return true;
}

inline bool slider_numeric_format(int64_t value, char* output, size_t output_size) {
    if (!output || !output_size) return false;
    const bool negative = value < 0;
    const uint64_t magnitude = negative
        ? static_cast<uint64_t>(-(value + 1)) + 1
        : static_cast<uint64_t>(value);
    const uint64_t whole = magnitude / SLIDER_FIXED_SCALE;
    const uint32_t fraction = static_cast<uint32_t>(magnitude % SLIDER_FIXED_SCALE);
    char fractional[5];
    std::snprintf(fractional, sizeof(fractional), "%04u", fraction);
    size_t fraction_length = 4;
    while (fraction_length && fractional[fraction_length - 1] == '0') {
        fractional[--fraction_length] = '\0';
    }
    char formatted[32];
    const int length = fraction_length
        ? std::snprintf(formatted, sizeof(formatted), "%s%llu.%s",
                        negative ? "-" : "",
                        static_cast<unsigned long long>(whole), fractional)
        : std::snprintf(formatted, sizeof(formatted), "%s%llu",
                        negative ? "-" : "",
                        static_cast<unsigned long long>(whole));
    if (length < 0 || static_cast<size_t>(length) >= output_size) return false;
    std::memcpy(output, formatted, static_cast<size_t>(length) + 1);
    return true;
}