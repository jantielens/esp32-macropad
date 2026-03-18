#ifndef WEB_PORTAL_PAGES_H
#define WEB_PORTAL_PAGES_H

#include <ESPAsyncWebServer.h>

void handleRoot(AsyncWebServerRequest *request);
void handleHome(AsyncWebServerRequest *request);
void handlePad(AsyncWebServerRequest *request);
void handleNetwork(AsyncWebServerRequest *request);
void handleFirmware(AsyncWebServerRequest *request);
void handleCSS(AsyncWebServerRequest *request);
void handleJS(AsyncWebServerRequest *request);

// Split JS module handlers
void handleCoreJS(AsyncWebServerRequest *request);
void handleConfigJS(AsyncWebServerRequest *request);
void handleFirmwareJS(AsyncWebServerRequest *request);
void handleHealthJS(AsyncWebServerRequest *request);
void handlePadColorsJS(AsyncWebServerRequest *request);
void handlePadIOJS(AsyncWebServerRequest *request);
void handlePadEditorJS(AsyncWebServerRequest *request);
void handleActionEditorJS(AsyncWebServerRequest *request);

#endif // WEB_PORTAL_PAGES_H
