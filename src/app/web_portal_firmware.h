#ifndef WEB_PORTAL_FIRMWARE_H
#define WEB_PORTAL_FIRMWARE_H

#include <ESPAsyncWebServer.h>

void handlePostFirmwareUpdate(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleGetFirmwareUpdateStatus(AsyncWebServerRequest *request);

// Internal status used to gate concurrent OTA flows.
bool web_portal_firmware_update_in_progress();

#endif // WEB_PORTAL_FIRMWARE_H
