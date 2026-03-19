#pragma once

#include "board_config.h"

#if HAS_SENSOR_HX711

#include <ESPAsyncWebServer.h>
#include <stdint.h>
#include <stddef.h>

// GET /api/brews — list all brews (fields only, no series)
void handleGetBrews(AsyncWebServerRequest* request);

// GET /api/brews?id=N — full brew report including series
void handleGetBrew(AsyncWebServerRequest* request);

// DELETE /api/brews?id=N — delete a specific brew
void handleDeleteBrew(AsyncWebServerRequest* request);

// DELETE /api/brews — clear all brew history
void handleDeleteAllBrews(AsyncWebServerRequest* request);

// POST /api/brews/import — import brew(s) from JSON
void handlePostBrewImport(AsyncWebServerRequest* request, uint8_t* data,
                          size_t len, size_t index, size_t total);

#endif // HAS_SENSOR_HX711
