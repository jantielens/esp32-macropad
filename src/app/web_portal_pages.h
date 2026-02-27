#ifndef WEB_PORTAL_PAGES_H
#define WEB_PORTAL_PAGES_H

#include <ESPAsyncWebServer.h>

void handleRoot(AsyncWebServerRequest *request);
void handleHome(AsyncWebServerRequest *request);
void handleNetwork(AsyncWebServerRequest *request);
void handleFirmware(AsyncWebServerRequest *request);
void handleCSS(AsyncWebServerRequest *request);
void handleJS(AsyncWebServerRequest *request);

#endif // WEB_PORTAL_PAGES_H
