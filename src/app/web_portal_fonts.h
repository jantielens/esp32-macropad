#pragma once
#include "board_config.h"
#if HAS_TTF_FONTS
#include <ESPAsyncWebServer.h>
void handleGetFonts(AsyncWebServerRequest* request);
void handleDeleteFont(AsyncWebServerRequest* request);
void handleFontUploadBody(AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t index, size_t total);
void handleFontUploadComplete(AsyncWebServerRequest* request);
#endif