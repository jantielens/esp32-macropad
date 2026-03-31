#pragma once

#include "board_config.h"

#if HAS_SCALE

#include <ESPAsyncWebServer.h>

// GET /api/brew-templates — list all templates (name, display_name, description, is_dynamic)
void handleGetBrewTemplates(AsyncWebServerRequest* request);

// GET /api/brew-templates/get?name=xxx — download single template as JSON
void handleGetBrewTemplate(AsyncWebServerRequest* request);

// POST /api/brew-templates — upload a template JSON body; validates, saves to LittleFS, reloads
void handlePostBrewTemplate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);

// DELETE /api/brew-templates?name=xxx — delete a template from LittleFS; built-in re-emerges if applicable
void handleDeleteBrewTemplate(AsyncWebServerRequest* request);

#endif // HAS_SCALE
