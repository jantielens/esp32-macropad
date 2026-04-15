// ============================================================================
// Test stub: Arduino.h — minimal shim for host-native compilation
// ============================================================================
// Provides just enough Arduino types so headers that include <Arduino.h>
// compile on the host without pulling in the real ESP32 SDK.

#ifndef ARDUINO_H
#define ARDUINO_H

#include <cstdint>
#include <cstddef>
#include <cstring>

// Mock millis() — test code sets this directly
extern uint32_t g_mock_millis;
inline uint32_t millis() { return g_mock_millis; }

#endif // ARDUINO_H
