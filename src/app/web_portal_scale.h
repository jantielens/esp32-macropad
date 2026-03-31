#pragma once

#include "board_config.h"

#if HAS_SCALE

#include <stdint.h>
#include <stddef.h>

class AsyncWebServerRequest;

// POST /api/scale/tare — zero the scale
void handlePostScaleTare(AsyncWebServerRequest* request);

// POST /api/scale/calibrate — body: { "known_weight_g": 100.0 }
void handlePostScaleCalibrate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

// GET /api/scale — current scale state
void handleGetScaleStatus(AsyncWebServerRequest* request);

#endif // HAS_SCALE
