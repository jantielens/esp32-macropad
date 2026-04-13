#pragma once

#include "board_config.h"
#include <stdint.h>

// ============================================================================
// MQTT Notify Control
// ============================================================================
// Exposes a text entity for remote notification triggering.
// Accepts plain text (default styling) or JSON (full control).
//
// Topics (relative to base topic):
//   ~/notify/set    (plain text or JSON)   ~/notify/state   (last message text)

#if HAS_DISPLAY && HAS_MQTT

void mqtt_notify_init();
void mqtt_notify_loop();
void mqtt_notify_on_message(const char* topic, const uint8_t* payload, unsigned int length);
void mqtt_notify_on_connected();

#else

static inline void mqtt_notify_init() {}
static inline void mqtt_notify_loop() {}
static inline void mqtt_notify_on_message(const char*, const uint8_t*, unsigned int) {}
static inline void mqtt_notify_on_connected() {}

#endif
