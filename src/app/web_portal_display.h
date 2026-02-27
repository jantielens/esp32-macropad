#ifndef WEB_PORTAL_DISPLAY_H
#define WEB_PORTAL_DISPLAY_H

#include "board_config.h"

#include <ESPAsyncWebServer.h>

#if HAS_DISPLAY

void handleSetDisplayBrightness(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleGetDisplaySleep(AsyncWebServerRequest *request);
void handlePostDisplaySleep(AsyncWebServerRequest *request);
void handlePostDisplayWake(AsyncWebServerRequest *request);
void handlePostDisplayActivity(AsyncWebServerRequest *request);
void handleSetDisplayScreen(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

#endif // HAS_DISPLAY

#endif // WEB_PORTAL_DISPLAY_H
