#pragma once

#include "board_config.h"

// Home Assistant MQTT buttons for saving a camera snapshot.
#if HAS_CAMERA && HAS_MQTT

void mqtt_camera_init();
void mqtt_camera_loop();
void mqtt_camera_on_message(const char* topic, const uint8_t* payload, unsigned int length);
void mqtt_camera_on_connected();

#else

inline void mqtt_camera_init() {}
inline void mqtt_camera_loop() {}
inline void mqtt_camera_on_message(const char*, const uint8_t*, unsigned int) {}
inline void mqtt_camera_on_connected() {}

#endif