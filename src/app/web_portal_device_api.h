#ifndef WEB_PORTAL_DEVICE_API_H
#define WEB_PORTAL_DEVICE_API_H

#include <ESPAsyncWebServer.h>

void handleGetMode(AsyncWebServerRequest *request);
void handleGetVersion(AsyncWebServerRequest *request);
void handleGetHealth(AsyncWebServerRequest *request);
void handleGetHealthHistory(AsyncWebServerRequest *request);
void handleReboot(AsyncWebServerRequest *request);

#endif // WEB_PORTAL_DEVICE_API_H
