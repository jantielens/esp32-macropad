#pragma once

#include "board_config.h"

#if HAS_MQTT && HAS_DISPLAY

#include <ESPAsyncWebServer.h>

void mqtt_inspection_init();
void mqtt_inspection_tick();
void mqtt_inspection_register_routes(AsyncWebServer* server);

#endif // HAS_MQTT && HAS_DISPLAY
