#ifndef WEB_PORTAL_OTA_H
#define WEB_PORTAL_OTA_H

#include <ESPAsyncWebServer.h>

// /api/update firmware upload handler
void handleOTAUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);

#endif // WEB_PORTAL_OTA_H
