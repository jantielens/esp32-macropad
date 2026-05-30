#pragma once

#include "board_config.h"

#if IS_COFFEE_SCALE

#include <ESPAsyncWebServer.h>
#include <stdint.h>
#include <stddef.h>

// POST /api/scale/tare — zero the scale
void handlePostScaleTare(AsyncWebServerRequest* request);

// POST /api/scale/calibrate — body: { "known_weight_g": 100.0 }
void handlePostScaleCalibrate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

// GET /api/scale — current scale state
void handleGetScaleStatus(AsyncWebServerRequest* request);

// Register all scale REST API routes on the web server.
void web_portal_scale_register_routes(AsyncWebServer* server);

#endif // IS_COFFEE_SCALE
