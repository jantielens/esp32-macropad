#include "voice_note_range.h"

#if HAS_VOICE_NOTES
#include <string.h>

namespace {
bool number(const char*& p, uint64_t& value) {
    if (*p < '0' || *p > '9') return false;
    value = 0;
    do {
        const unsigned digit = *p++ - '0';
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    } while (*p >= '0' && *p <= '9');
    return true;
}
}

bool voice_note_parse_range(const char* header, uint64_t size,
                            uint64_t* start, uint64_t* end) {
    if (!header || !start || !end || !size || strncmp(header, "bytes=", 6)) return false;
    const char* p = header + 6;
    uint64_t first = 0;
    uint64_t last = 0;
    if (*p == '-') {
        ++p;
        if (!number(p, last) || *p || !last) return false;
        *start = last >= size ? 0 : size - last;
        *end = size - 1;
        return true;
    }
    if (!number(p, first) || *p++ != '-' || first >= size) return false;
    if (*p) {
        if (!number(p, last) || *p || last < first) return false;
        if (last >= size) last = size - 1;
    } else {
        last = size - 1;
    }
    *start = first;
    *end = last;
    return true;
}
#endif
