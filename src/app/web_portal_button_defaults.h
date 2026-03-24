#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include <ESPAsyncWebServer.h>

void handleGetButtonDefaults(AsyncWebServerRequest *request);
void handlePostButtonDefaults(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

#endif // HAS_DISPLAY
