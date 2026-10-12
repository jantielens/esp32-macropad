#pragma once

#include <stddef.h>
#include <string.h>

inline bool portal_path_matches(const char* url, const char* prefix,
                                size_t segments, bool restricted = false) {
    const size_t prefix_length = strlen(prefix);
    if (strncmp(url, prefix, prefix_length) != 0) return false;
    const char* cursor = url + prefix_length;
    for (size_t segment = 0; segment < segments; ++segment) {
        const char* start = cursor;
        while (*cursor && *cursor != '/') {
            if (restricted && !((*cursor >= 'a' && *cursor <= 'z') ||
                                (*cursor >= '0' && *cursor <= '9') || *cursor == '-')) {
                return false;
            }
            ++cursor;
        }
        if (cursor == start) return false;
        if (segment + 1 == segments) return *cursor == '\0';
        if (*cursor != '/') return false;
        ++cursor;
    }
    return false;
}

template <typename StringType>
StringType portal_path_arg(const StringType& url, const char* prefix, size_t index) {
    size_t start = strlen(prefix);
    while (index--) {
        const int separator = url.indexOf('/', start);
        if (separator < 0) return StringType();
        start = static_cast<size_t>(separator) + 1;
    }
    const int end = url.indexOf('/', start);
    return end < 0 ? url.substring(start) : url.substring(start, end);
}