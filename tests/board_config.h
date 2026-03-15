// ============================================================================
// Test stub: board_config.h — shadows the real ESP32 board_config.h
// ============================================================================
// Provides minimal defines so binding code compiles on the host.

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define HAS_DISPLAY true
#define HAS_MQTT    true
#define HAS_BLE_HID true

// strlcpy is available on ESP32 (newlib) but not glibc — declare for host tests
#include <stddef.h>
#ifdef __cplusplus
extern "C"
#endif
size_t strlcpy(char* dst, const char* src, size_t siz);

#endif // BOARD_CONFIG_H
