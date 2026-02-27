#ifndef WEB_PORTAL_CONFIG_H
#define WEB_PORTAL_CONFIG_H

#include <ESPAsyncWebServer.h>

void handleGetConfig(AsyncWebServerRequest *request);
void handlePostConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleDeleteConfig(AsyncWebServerRequest *request);

// Called from the main loop to cleanup stale/chunked /api/config uploads.
void web_portal_config_loop();

#endif // WEB_PORTAL_CONFIG_H
