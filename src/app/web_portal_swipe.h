#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include <ESPAsyncWebServer.h>

void handleGetSwipeActions(AsyncWebServerRequest *request);
void handlePostSwipeActions(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

#endif // HAS_DISPLAY
