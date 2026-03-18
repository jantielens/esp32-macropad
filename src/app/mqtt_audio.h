#pragma once

#include <stdint.h>
#include "board_config.h"

// ============================================================================
// MQTT Audio Control
// ============================================================================
// Exposes audio as HA entities:
//   - siren:  looping beep pattern with turn_on/turn_off, optional tone + volume + duration
//   - number: persistent device volume (0-100)
//   - text:   custom tone DSL pattern (used when siren tone is "custom")
//   - button: one-click single / double / triple beep
//
// Topics (relative to base topic):
//   ~/audio/siren/set         (JSON command)  ~/audio/siren/state        (ON/OFF)
//   ~/audio/volume/set        (plain number)  ~/audio/volume/state       (plain number)
//   ~/audio/custom_tone/set   (DSL string)    ~/audio/custom_tone/state  (DSL string)
//   ~/audio/beep              (PRESS payload — single beep)
//   ~/audio/beep_double       (PRESS payload — double beep)
//   ~/audio/beep_triple       (PRESS payload — triple beep)

#if HAS_AUDIO && HAS_MQTT

void mqtt_audio_init();
void mqtt_audio_loop();
void mqtt_audio_on_message(const char* topic, const uint8_t* payload, unsigned int length);
void mqtt_audio_on_connected();

#else

inline void mqtt_audio_init() {}
inline void mqtt_audio_loop() {}
inline void mqtt_audio_on_message(const char*, const uint8_t*, unsigned int) {}
inline void mqtt_audio_on_connected() {}

#endif
