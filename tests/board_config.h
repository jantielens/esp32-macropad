// ============================================================================
// Test stub: board_config.h — shadows the real ESP32 board_config.h
// ============================================================================
// Provides minimal defines so binding code compiles on the host.

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define HAS_DISPLAY true
#define HAS_MQTT    true
#define HAS_BLE_HID true

// strlcpy is available on ESP32 (newlib) but not older glibc — declare for host
// tests. glibc 2.38+ (Ubuntu 24.04) ships strlcpy natively, so skip when present.
#include <stddef.h>
#include <string.h>
#if !defined(__GLIBC__) || (__GLIBC__ < 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 38)
#ifdef __cplusplus
extern "C"
#endif
size_t strlcpy(char* dst, const char* src, size_t siz);
#endif

#endif // BOARD_CONFIG_H
