// ============================================================================
// Test stubs — libc functions missing on glibc (Linux)
// ============================================================================

#include <cstring>
#include <cstddef>

// strlcpy is available on ESP32 (newlib) and BSD but not older glibc.
// glibc 2.38+ (Ubuntu 24.04) provides it natively — skip our fallback.
#if !defined(__GLIBC__) || (__GLIBC__ < 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 38)
extern "C" size_t strlcpy(char* dst, const char* src, size_t siz) {
    size_t len = strlen(src);
    if (siz > 0) {
        size_t copy = len < siz - 1 ? len : siz - 1;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return len;
}
#endif

#if IS_SHUTTER_TESTER
// Weak stubs for shutter_session_actions hooks called from shutter_session.cpp.
// Real implementation lives in src/app/shutter_session_actions.cpp which has
// flash / FreeRTOS / LVGL dependencies that the host tests do not link.
void shutter_session_actions_dispatch_start() {}
void shutter_session_actions_notify_complete() {}
#endif
