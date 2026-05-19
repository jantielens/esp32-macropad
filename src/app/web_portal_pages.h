#ifndef WEB_PORTAL_PAGES_H
#define WEB_PORTAL_PAGES_H

#include <ESPAsyncWebServer.h>

// Legacy page handlers (kept for backward-compatible redirects)
void handleRoot(AsyncWebServerRequest *request);
void handleHome(AsyncWebServerRequest *request);
void handlePad(AsyncWebServerRequest *request);
void handleNetwork(AsyncWebServerRequest *request);
void handleFirmware(AsyncWebServerRequest *request);

// Asset handlers
void handleJS(AsyncWebServerRequest *request);
void handlePortalAllCSS(AsyncWebServerRequest *request);

// Shell handler (new single-page root)
void handleShell(AsyncWebServerRequest *request);

// Fragment handler — serves gzipped fragment HTML by id
void handleFragment(AsyncWebServerRequest *request);

#endif // WEB_PORTAL_PAGES_H
