#include "web_portal_ble.h"
#include "board_config.h"

#if HAS_BLE_HID

#include "ble_hid.h"
#include "web_portal_cors.h"
#include "log_manager.h"

#include <ESPAsyncWebServer.h>

#define TAG "WebBLE"

void handlePostBlePairingStart(AsyncWebServerRequest* request) {
    if (!ble_hid_is_initialized()) {
        LOGW(TAG, "BLE pairing rejected: BLE disabled in settings");
        AsyncWebServerResponse* response = request->beginResponse(400, "application/json", "{\"ok\":false,\"error\":\"BLE disabled\"}");
        web_portal_add_cors_headers(response);
        request->send(response);
        return;
    }
    LOGI(TAG, "BLE pairing requested via portal");
    ble_hid_request_pairing();
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", "{\"ok\":true}");
    web_portal_add_cors_headers(response);
    request->send(response);
}

#endif // HAS_BLE_HID
