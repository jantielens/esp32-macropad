#include "web_portal_ble.h"
#include "board_config.h"

#if HAS_BLE_HID

#include "ble_hid.h"
#include "keyboard_hid.h"
#include "web_portal_cors.h"
#include "log_manager.h"

#include <ESPAsyncWebServer.h>

#define TAG "WebBLE"

void handlePostBlePairingStart(AsyncWebServerRequest* request) {
    if (!ble_hid_is_initialized() || keyboard_hid_transport() != KeyboardTransport::Ble || keyboard_hid_is_busy()) {
        LOGW(TAG, "BLE pairing rejected: backend inactive or keyboard busy");
        AsyncWebServerResponse* response = request->beginResponse(keyboard_hid_is_busy() ? 409 : 400, "application/json", "{\"success\":false,\"error\":\"BLE inactive or keyboard busy\"}");
        web_portal_add_cors_headers(response);
        request->send(response);
        return;
    }
    LOGI(TAG, "BLE pairing requested via portal");
    ble_hid_request_pairing();
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", "{\"success\":true}");
    web_portal_add_cors_headers(response);
    request->send(response);
}

#endif // HAS_BLE_HID
