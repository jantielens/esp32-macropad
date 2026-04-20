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

// Controllable millis() mock for timer tests
#ifdef __cplusplus
extern "C" {
#endif
unsigned long millis();
#ifdef __cplusplus
}
#endif

#endif // ARDUINO_H
