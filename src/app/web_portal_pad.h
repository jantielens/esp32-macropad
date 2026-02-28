#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include <ESPAsyncWebServer.h>

// GET /api/pad?page=N — return raw JSON config for page N
void handleGetPadConfig(AsyncWebServerRequest *request);

// POST /api/pad?page=N — save JSON config for page N (body handler)
void handlePostPadConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

// DELETE /api/pad?page=N — delete config for page N
void handleDeletePadConfig(AsyncWebServerRequest *request);

#endif // HAS_DISPLAY
