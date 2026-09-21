#pragma once

#include <ESPAsyncWebServer.h>
#include "board_config.h"

#if HAS_DISPLAY
void handleGetRecipeCatalog(AsyncWebServerRequest* request);
void handlePostRecipeCatalog(AsyncWebServerRequest* request, uint8_t* data,
                             size_t len, size_t index, size_t total);
#endif