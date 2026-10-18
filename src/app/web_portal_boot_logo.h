#pragma once

#include "board_config.h"
#include <ESPAsyncWebServer.h>

#if HAS_BOOT_LOGO
void handleGetBootLogo(AsyncWebServerRequest* request);
void handleDeleteBootLogo(AsyncWebServerRequest* request);
void handleGetBootLogoThumbnail(AsyncWebServerRequest* request);
void handlePostBootLogoUpload(AsyncWebServerRequest* request, uint8_t* data,
		size_t length, size_t index, size_t total);
void handlePostBootLogoUploadComplete(AsyncWebServerRequest* request);
#endif
