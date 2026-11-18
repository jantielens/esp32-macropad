#pragma once

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

// MCP volume accepts decimal strings as well as integers. Clamp before
// converting so very large finite values remain safe.
inline bool mcp_volume_percent(const char* text, int32_t* percent) {
    if (!text || !text[0] || !percent) return false;
    errno = 0;
    char* end = nullptr;
    const double number = strtod(text, &end);
    if (end == text || errno == ERANGE || !isfinite(number)) return false;
    while (isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end) return false;
    if (number >= 100) *percent = 100;
    else if (number <= -100) *percent = -100;
    else *percent = static_cast<int32_t>(roundf(static_cast<float>(number)));
    return true;
}
