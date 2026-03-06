// ============================================================================
// Test stubs — libc functions missing on glibc (Linux)
// ============================================================================

#include <cstring>
#include <cstddef>

// strlcpy is available on ESP32 (newlib) and BSD but not glibc.
extern "C" size_t strlcpy(char* dst, const char* src, size_t siz) {
    size_t len = strlen(src);
    if (siz > 0) {
        size_t copy = len < siz - 1 ? len : siz - 1;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return len;
}
