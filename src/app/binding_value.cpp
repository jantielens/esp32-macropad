#include "binding_value.h"

#include "psram_json_allocator.h"
#include <ArduinoJson.h>
#include <errno.h>
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

static bool copy_value(const char* value, char* out, size_t out_len) {
    return copy_part(value, strlen(value), out, out_len);
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
        return copy_value(payload, out, out_len);
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
        return copy_value(value.as<const char*>(), out, out_len);
    } else if (value.is<float>() || value.is<double>()) {
        const int length = snprintf(out, out_len, "%g", value.as<double>());
        return length >= 0 && (size_t)length < out_len;
    } else if (value.is<bool>()) {
        return copy_value(value.as<bool>() ? "true" : "false", out, out_len);
    } else {
        const size_t written = serializeJson(value, out, out_len);
        if (written == out_len) {
            out[out_len - 1] = '\0';
            return false;
        }
    }
    return true;
}

#if HAS_EXTERNAL_DATA
static JsonVariantConst select_json_path(JsonVariantConst value, const char* path) {
    if (!path) return JsonVariantConst();
    const char* token = path;
    if (strcmp(path, ".") == 0) token += 1;
    while (*token) {
        const size_t length = strcspn(token, ".");
        if (!length) return JsonVariantConst();
        if (value.is<JsonObjectConst>()) {
            value = value[JsonString(token, length)];
        } else if (value.is<JsonArrayConst>()) {
            size_t index = 0;
            for (size_t i = 0; i < length; ++i) {
                if (token[i] < '0' || token[i] > '9') return JsonVariantConst();
                const unsigned digit = token[i] - '0';
                if (index > (SIZE_MAX - digit) / 10) return JsonVariantConst();
                index = index * 10 + digit;
            }
            value = value[index];
        } else {
            return JsonVariantConst();
        }
        token += length;
        if (*token == '.') {
            ++token;
            if (!*token) return JsonVariantConst();
        }
    }
    return value;
}

bool binding_value_select_json(JsonVariantConst value, const char* path,
                               char* out, size_t out_len) {
    if (!path || !out || !out_len) return false;
    out[0] = '\0';
    value = select_json_path(value, path);
    if (value.isNull()) return false;
    if (value.is<const char*>()) return copy_value(value.as<const char*>(), out, out_len);
    if (value.is<JsonObjectConst>() || value.is<JsonArrayConst>()) {
        if (measureJson(value) >= out_len) return false;
        return serializeJson(value, out, out_len) < out_len;
    }
    if (value.is<bool>()) return copy_value(value.as<bool>() ? "true" : "false", out, out_len);
    if (value.is<int64_t>()) {
        const int length = snprintf(out, out_len, "%lld", static_cast<long long>(value.as<int64_t>()));
        return length >= 0 && static_cast<size_t>(length) < out_len;
    }
    if (value.is<uint64_t>()) {
        const int length = snprintf(out, out_len, "%llu", static_cast<unsigned long long>(value.as<uint64_t>()));
        return length >= 0 && static_cast<size_t>(length) < out_len;
    }
    const double number = value.as<double>();
    const bool fits_float = static_cast<double>(static_cast<float>(number)) == number;
    const int first_precision = fits_float ? 6 : 15;
    const int last_precision = fits_float ? std::numeric_limits<float>::max_digits10
                                          : std::numeric_limits<double>::max_digits10;
    for (int precision = first_precision; precision <= last_precision; ++precision) {
        const int length = snprintf(out, out_len, "%.*g", precision, number);
        if (length < 0 || static_cast<size_t>(length) >= out_len) return false;
        const double parsed = strtod(out, nullptr);
        if ((fits_float && static_cast<float>(parsed) == static_cast<float>(number)) ||
            (!fits_float && parsed == number)) return true;
    }
    return false;
}

#if HAS_MCP
#include "pad_config.h"
static constexpr size_t INSPECT_PAGE_SIZE = 8;
static constexpr size_t INSPECT_TEXT_BYTES = 96;

static size_t inspect_text_length(JsonString text) {
    if (text.size() <= INSPECT_TEXT_BYTES) return text.size();
    size_t length = INSPECT_TEXT_BYTES;
    while (length && (static_cast<unsigned char>(text.c_str()[length]) & 0xc0) == 0x80) --length;
    return length;
}

static void inspect_value(JsonObject output, JsonVariantConst value) {
    if (value.isNull()) output["type"] = "null";
    else if (value.is<JsonObjectConst>()) {
        output["type"] = "object";
        output["size"] = value.as<JsonObjectConst>().size();
    } else if (value.is<JsonArrayConst>()) {
        output["type"] = "array";
        output["size"] = value.as<JsonArrayConst>().size();
    } else if (value.is<const char*>()) {
        output["type"] = "string";
        const JsonString text = value.as<JsonString>();
        const size_t length = inspect_text_length(text);
        output["value"] = JsonString(text.c_str(), length, JsonString::Copied);
        if (text.size() > length) output["value_truncated"] = true;
    } else {
        output["type"] = value.is<bool>() ? "boolean" : "number";
        output["value"] = value;
    }
}

static void inspect_child(JsonObject output, const char* path, JsonString name,
                          JsonVariantConst value) {
    const size_t path_len = strcmp(path, ".") == 0 ? 0 : strlen(path);
    const size_t name_len = name.size();
    const size_t shown = inspect_text_length(name);
    output["key"] = JsonString(name.c_str(), shown, JsonString::Copied);
    if (name_len > shown) output["key_truncated"] = true;
    bool bindable = name_len && path_len + (path_len ? 1 : 0) + name_len < CONFIG_JSON_PATH_MAX_LEN;
    for (size_t i = 0; i < name_len && bindable; ++i) {
        const char c = name.c_str()[i];
        if (!c || c == '.' || c == ';' || c == '[' || c == ']' || c == '|') bindable = false;
    }
    output["bindable"] = bindable && !value.isNull();
    if (bindable) {
        char child_path[CONFIG_JSON_PATH_MAX_LEN];
        size_t used = 0;
        if (path_len) {
            memcpy(child_path, path, path_len);
            used = path_len;
            child_path[used++] = '.';
        }
        memcpy(child_path + used, name.c_str(), name_len);
        child_path[used + name_len] = '\0';
        output["path"] = JsonString(child_path, JsonString::Copied);
    }
    inspect_value(output, value);
}

bool binding_value_inspect_json(JsonVariantConst root, const char* path,
                                size_t offset, JsonObject result) {
    if (!path || strlen(path) >= CONFIG_JSON_PATH_MAX_LEN || strpbrk(path, ";[]|")) return false;
    JsonVariantConst value = select_json_path(root, path);
    if (value.isNull()) return false;
    inspect_value(result, value);
    size_t count = 0;
    if (value.is<JsonObjectConst>()) count = value.as<JsonObjectConst>().size();
    else if (value.is<JsonArrayConst>()) count = value.as<JsonArrayConst>().size();
    else return true;

    JsonArray children = result["children"].to<JsonArray>();
    size_t index = 0;
    if (value.is<JsonObjectConst>()) {
        for (JsonPairConst pair : value.as<JsonObjectConst>()) {
            if (index >= offset && index - offset < INSPECT_PAGE_SIZE)
                inspect_child(children.add<JsonObject>(), path, pair.key(), pair.value());
            if (++index >= offset && index - offset >= INSPECT_PAGE_SIZE) break;
        }
    } else {
        for (JsonVariantConst child : value.as<JsonArrayConst>()) {
            if (index >= offset && index - offset < INSPECT_PAGE_SIZE) {
                char name[24];
                snprintf(name, sizeof(name), "%u", static_cast<unsigned>(index));
                inspect_child(children.add<JsonObject>(), path, JsonString(name), child);
            }
            if (++index >= offset && index - offset >= INSPECT_PAGE_SIZE) break;
        }
    }
    result["offset"] = offset;
    if (offset < count && count - offset > INSPECT_PAGE_SIZE)
        result["next_offset"] = offset + INSPECT_PAGE_SIZE;
    return true;
}
#endif
#endif

enum class Length { NONE, HH, H, L, LL, J, Z, T, LONG_DOUBLE };

static bool skip_digits(const char*& p, unsigned maximum = INT_MAX) {
    unsigned int value = 0;
    while (*p >= '0' && *p <= '9') {
        const unsigned int digit = *p++ - '0';
        if (value > (INT_MAX - digit) / 10u) return false;
        value = value * 10 + digit;
    }
    return value <= maximum;
}

static bool parse_format(const char* format, char& conversion, Length& length,
                         unsigned max_width = INT_MAX, unsigned max_precision = INT_MAX) {
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
        if (!skip_digits(p, max_width)) return false;
        if (*p == '.') {
            ++p;
            if (!skip_digits(p, max_precision)) return false;
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

bool binding_value_format_valid(const char* format, unsigned max_width, unsigned max_precision,
                                const char* conversions) {
    char conversion = '\0';
    Length length = Length::NONE;
    return format && conversions && parse_format(format, conversion, length, max_width, max_precision)
        && strchr(conversions, conversion);
}

static void binding_value_format_impl(const char* raw_value, const char* format, char* out,
                                      size_t out_len, bool scientific_numbers) {
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

    const bool is_signed = conversion == 'd' || conversion == 'i';
    unsigned long long unsigned_value = 0;
    bool unsigned_integer = false;
    if (scientific_numbers && !is_signed && raw_value[0] >= '0' && raw_value[0] <= '9') {
        errno = 0;
        unsigned_value = strtoull(raw_value, &end, 10);
        unsigned_integer = *end == '\0';
        if (unsigned_integer && errno == ERANGE) {
            copy_value(raw_value, out, out_len);
            return;
        }
    }
    if (scientific_numbers) errno = 0;
    long long value = unsigned_integer ? 0 : strtoll(raw_value, &end, 10);
    if (scientific_numbers && !unsigned_integer && *end == '\0' && errno == ERANGE) {
        copy_value(raw_value, out, out_len);
        return;
    }
    char* number_end = nullptr;
    const double number = scientific_numbers ? strtod(raw_value, &number_end) : 0;
    const bool scientific = scientific_numbers && number_end && !*number_end &&
                            number_end != raw_value && strpbrk(raw_value, "eE");
    if (!unsigned_integer && (end == raw_value || scientific)) {
        const double parsed = scientific ? number : strtod(raw_value, &end);
        // Preserve MQTT's decimal prefix behavior ("1.9" becomes 1) above,
        // while allowing floating values without an integer prefix (".5").
        if (scientific && !is_signed && isfinite(parsed) && parsed >= 0 &&
            static_cast<long double>(parsed) < -2.0L * std::numeric_limits<long long>::lowest()) {
            unsigned_value = static_cast<unsigned long long>(parsed);
            unsigned_integer = true;
        } else if ((!scientific && end == raw_value) || !isfinite(parsed) ||
            static_cast<long double>(parsed) < std::numeric_limits<long long>::lowest() ||
            static_cast<long double>(parsed) >= -static_cast<long double>(std::numeric_limits<long long>::lowest())) {
            copy_value(raw_value, out, out_len);
            return;
        } else {
            value = static_cast<long long>(parsed);
        }
    }
    if (!unsigned_integer) unsigned_value = static_cast<unsigned long long>(value);
    // Match each variadic argument to its conversion, including default
    // promotions for h/hh; never pass a long to an unmodified %d or a double to %Lf.
    switch (length) {
        case Length::HH:
            if (is_signed) snprintf(out, out_len, format, static_cast<signed char>(value));
            else snprintf(out, out_len, format, static_cast<unsigned char>(unsigned_value));
            break;
        case Length::H:
            if (is_signed) snprintf(out, out_len, format, static_cast<short>(value));
            else snprintf(out, out_len, format, static_cast<unsigned short>(unsigned_value));
            break;
        case Length::NONE:
            if (is_signed) snprintf(out, out_len, format, static_cast<int>(value));
            else snprintf(out, out_len, format, static_cast<unsigned int>(unsigned_value));
            break;
        case Length::L:
            if (is_signed) snprintf(out, out_len, format, static_cast<long>(value));
            else snprintf(out, out_len, format, static_cast<unsigned long>(unsigned_value));
            break;
        case Length::LL:
            if (is_signed) snprintf(out, out_len, format, value);
            else snprintf(out, out_len, format, unsigned_value);
            break;
        case Length::J:
            if (is_signed) snprintf(out, out_len, format, static_cast<intmax_t>(value));
            else snprintf(out, out_len, format, static_cast<uintmax_t>(unsigned_value));
            break;
        case Length::Z:
            if (is_signed) snprintf(out, out_len, format, static_cast<std::make_signed<size_t>::type>(value));
            else snprintf(out, out_len, format, static_cast<size_t>(unsigned_value));
            break;
        case Length::T:
            if (is_signed) snprintf(out, out_len, format, static_cast<ptrdiff_t>(value));
            else snprintf(out, out_len, format, static_cast<std::make_unsigned<ptrdiff_t>::type>(unsigned_value));
            break;
        case Length::LONG_DOUBLE: break;
    }
}

void binding_value_format(const char* raw_value, const char* format, char* out, size_t out_len) {
    binding_value_format_impl(raw_value, format, out, out_len, false);
}

#if HAS_EXTERNAL_DATA
void binding_value_format_external(const char* raw_value, const char* format, char* out, size_t out_len) {
    binding_value_format_impl(raw_value, format, out, out_len, true);
}
#endif
