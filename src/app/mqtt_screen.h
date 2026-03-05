#pragma once

#include <stdint.h>
#include "board_config.h"

// ============================================================================
// MQTT Screen Control
// ============================================================================
// Exposes the active screen as an HA select entity. The device publishes
// screen changes to ~/screen/state (retained). HA can send a screen ID to
// ~/screen/set to navigate the device and wake the screensaver.

#if HAS_MQTT && HAS_DISPLAY

// Initialize topics. Call after mqtt_manager.begin().
void mqtt_screen_init();

// Poll for pending HA commands and publish state on screen change.
// Call from loop().
void mqtt_screen_loop();

// Called from the MQTT message callback for every incoming message.
// Checks if the topic matches the command topic and queues the request.
void mqtt_screen_on_message(const char* topic, const uint8_t* payload, unsigned int length);

// Subscribe command topic and publish initial state. Call on MQTT connect.
void mqtt_screen_on_connected();

#else

inline void mqtt_screen_init() {}
inline void mqtt_screen_loop() {}
inline void mqtt_screen_on_message(const char*, const uint8_t*, unsigned int) {}
inline void mqtt_screen_on_connected() {}

#endif
