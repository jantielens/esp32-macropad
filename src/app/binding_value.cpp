#include "binding_value.h"

#include "psram_json_allocator.h"
#include <ArduinoJson.h>
#include <limits.h>
#include <limits>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

static bool copy_part(const char* value, size_t len, char* out, size_t out_len) {
    const size_t copied = len < out_len ? len : out_len - 1;
    memcpy(out, value, copied);
    out[copied] = '\0';
    return len < out_len;
}

static void copy_value(const char* value, char* out, size_t out_len) {
    copy_part(value, strlen(value), out, out_len);
}

bool binding_value_parse_params(const char* params,
                                char* entity, size_t entity_len,
                                char* path, size_t path_len,
                                char* format, size_t format_len) {
    if (entity && entity_len) entity[0] = '\0';
    if (path && path_len) path[0] = '\0';
    if (format && format_len) format[0] = '\0';
    if (!params || !entity || !entity_len || !path || !path_len || !format || !format_len) return false;

    const char* first = strchr(params, ';');
    if (!first) return copy_part(params, strlen(params), entity, entity_len);
    bool fits = copy_part(params, first - params, entity, entity_len);
    const char* second = strchr(first + 1, ';');
    if (!second) return copy_part(first + 1, strlen(first + 1), path, path_len) && fits;
    fits = copy_part(first + 1, second - first - 1, path, path_len) && fits;
    return copy_part(second + 1, strlen(second + 1), format, format_len) && fits;
}

bool binding_value_extract_json(const char* payload, const char* path, char* out, size_t out_len) {
    if (!payload || !path || !out || out_len == 0) return false;
    if (path[0] == '.' && path[1] == '\0') {
        copy_value(payload, out, out_len);
        return true;
    }

    BasicJsonDocument<PsramJsonAllocator> doc(2048);
    if (deserializeJson(doc, payload)) {
        copy_value(payload, out, out_len);
        return false;
    }

    JsonVariant value = doc.as<JsonVariant>();
    const char* token = path;
    while (*token && !value.isNull()) {
        token += strspn(token, ".");
        if (!*token) break;
        const size_t length = strcspn(token, ".");
        if (value.is<JsonObject>()) value = value[JsonString(token, length)];
        else value = JsonVariant();
        token += length;
    }
    if (value.isNull()) {
        out[0] = '\0';
        return false;
    }
    if (value.is<const char*>()) {
        copy_value(value.as<const char*>(), out, out_len);
    } else if (value.is<float>() || value.is<double>()) {
        snprintf(out, out_len, "%g", value.as<double>());
    } else if (value.is<bool>()) {
        copy_value(value.as<bool>() ? "true" : "false", out, out_len);
    } else {
        const size_t written = serializeJson(value, out, out_len);
        if (written == out_len) out[out_len - 1] = '\0';
    }
    return true;
}

enum class Length { NONE, HH, H, L, LL, J, Z, T, LONG_DOUBLE };

static bool skip_digits(const char*& p) {
    unsigned int value = 0;
    while (*p >= '0' && *p <= '9') {
        const unsigned int digit = *p++ - '0';
        if (value > (INT_MAX - digit) / 10u) return false;
        value = value * 10 + digit;
    }
    return true;
}

static bool parse_format(const char* format, char& conversion, Length& length) {
    bool found = false;
    for (const char* p = format; *p; ++p) {
        if (*p != '%') continue;
        if (*++p == '%') continue;
        if (found || !*p) return false;
        found = true;
        bool sign = false, alternate = false, zero = false;
        while (*p && strchr("-+ #0", *p)) {
            sign |= *p == '+' || *p == ' ';
            alternate |= *p == '#';
            zero |= *p == '0';
            ++p;
        }
        if (!skip_digits(p)) return false;
        if (*p == '.') {
            ++p;
            if (!skip_digits(p)) return false;
        }
        length = Length::NONE;
        switch (*p) {
            case 'h': length = *++p == 'h' ? (++p, Length::HH) : Length::H; break;
            case 'l': length = *++p == 'l' ? (++p, Length::LL) : Length::L; break;
            case 'j': length = Length::J; ++p; break;
            case 'z': length = Length::Z; ++p; break;
            case 't': length = Length::T; ++p; break;
            case 'L': length = Length::LONG_DOUBLE; ++p; break;
        }
        conversion = *p;
        if (!conversion) return false;
        if (conversion == 's') {
            if (length != Length::NONE || sign || alternate || zero) return false;
        } else if (strchr("diuxXo", conversion)) {
            if (length == Length::LONG_DOUBLE) return false;
            if (strchr("di", conversion) ? alternate : sign) return false;
            if (conversion == 'u' && alternate) return false;
        } else if (strchr("fFeEgGaA", conversion)) {
            if (length != Length::NONE && length != Length::L && length != Length::LONG_DOUBLE) return false;
        } else {
            return false;
        }
    }
    return found;
}

void binding_value_format(const char* raw_value, const char* format, char* out, size_t out_len) {
    if (!raw_value || !out || !out_len) return;
    char conversion = '\0';
    Length length = Length::NONE;
    if (!format || !parse_format(format, conversion, length)) {
        copy_value(raw_value, out, out_len);
        return;
    }
    if (conversion == 's') {
        snprintf(out, out_len, format, raw_value);
        return;
    }
    char* end = nullptr;
    if (strchr("fFeEgGaA", conversion)) {
        const double value = strtod(raw_value, &end);
        if (end == raw_value || !isfinite(value)) {
            copy_value(raw_value, out, out_len);
        } else if (length == Length::LONG_DOUBLE) {
            snprintf(out, out_len, format, static_cast<long double>(value));
        } else {
            snprintf(out, out_len, format, value);
        }
        return;
    }

    long long value = strtoll(raw_value, &end, 10);
    if (end == raw_value) {
        const double number = strtod(raw_value, &end);
        // Preserve MQTT's decimal prefix behavior ("1.9" becomes 1) above,
        // while allowing floating values without an integer prefix (".5").
        if (end == raw_value || !isfinite(number) ||
            static_cast<long double>(number) < std::numeric_limits<long long>::lowest() ||
            static_cast<long double>(number) >= -static_cast<long double>(std::numeric_limits<long long>::lowest())) {
            copy_value(raw_value, out, out_len);
            return;
        }
        value = static_cast<long long>(number);
    }
    const bool is_signed = conversion == 'd' || conversion == 'i';
    // Match each variadic argument to its conversion, including default
    // promotions for h/hh; never pass a long to an unmodified %d or a double to %Lf.
    switch (length) {
        case Length::HH:
            if (is_signed) snprintf(out, out_len, format, static_cast<signed char>(value));
            else snprintf(out, out_len, format, static_cast<unsigned char>(value));
            break;
        case Length::H:
            if (is_signed) snprintf(out, out_len, format, static_cast<short>(value));
            else snprintf(out, out_len, format, static_cast<unsigned short>(value));
            break;
        case Length::NONE:
            if (is_signed) snprintf(out, out_len, format, static_cast<int>(value));
            else snprintf(out, out_len, format, static_cast<unsigned int>(value));
            break;
        case Length::L:
            if (is_signed) snprintf(out, out_len, format, static_cast<long>(value));
            else snprintf(out, out_len, format, static_cast<unsigned long>(value));
            break;
        case Length::LL:
            if (is_signed) snprintf(out, out_len, format, value);
            else snprintf(out, out_len, format, static_cast<unsigned long long>(value));
            break;
        case Length::J:
            if (is_signed) snprintf(out, out_len, format, static_cast<intmax_t>(value));
            else snprintf(out, out_len, format, static_cast<uintmax_t>(value));
            break;
        case Length::Z:
            if (is_signed) snprintf(out, out_len, format, static_cast<std::make_signed<size_t>::type>(value));
            else snprintf(out, out_len, format, static_cast<size_t>(value));
            break;
        case Length::T:
            if (is_signed) snprintf(out, out_len, format, static_cast<ptrdiff_t>(value));
            else snprintf(out, out_len, format, static_cast<std::make_unsigned<ptrdiff_t>::type>(value));
            break;
        case Length::LONG_DOUBLE: break;
    }
}
