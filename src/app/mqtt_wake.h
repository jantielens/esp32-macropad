#pragma once

#include "board_config.h"

// ============================================================================
// MQTT Wake — binding-driven screensaver wakeup
// ============================================================================
// Resolves a user-configured binding expression each loop tick.
// When the resolved value transitions to "ON", wakes the screensaver.
// While "ON" persists, resets the idle timer (~1 s) to keep the screen on.
// When the value leaves "ON", the normal idle timeout resumes.
// Supports any binding scheme: [mqtt:...], [expr:...], etc.

#if HAS_MQTT && HAS_DISPLAY

#include "config_manager.h"

// Initialize with config (must remain valid). Call after binding schemes are registered.
void mqtt_wake_init(const DeviceConfig* config);

// Poll binding, wake on OFF→ON edge, keep-alive while ON. Call from loop().
void mqtt_wake_loop();

#else

inline void mqtt_wake_init(const DeviceConfig*) {}
inline void mqtt_wake_loop() {}

#endif
