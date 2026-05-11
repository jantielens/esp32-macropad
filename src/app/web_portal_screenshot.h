#ifndef WEB_PORTAL_SCREENSHOT_H
#define WEB_PORTAL_SCREENSHOT_H

#include "board_config.h"

#if HAS_DISPLAY

#include <ESPAsyncWebServer.h>

void handleGetScreenshot(AsyncWebServerRequest *request);

#endif // HAS_DISPLAY
#endif // WEB_PORTAL_SCREENSHOT_H
