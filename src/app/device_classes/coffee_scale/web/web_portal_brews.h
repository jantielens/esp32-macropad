#pragma once

#include "board_config.h"

#if IS_COFFEE_SCALE

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

// Register all brew log REST API routes on the web server.
void web_portal_brews_register_routes(AsyncWebServer* server);

#endif // IS_COFFEE_SCALE
