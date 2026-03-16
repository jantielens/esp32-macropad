#pragma once

#include "board_config.h"

#if HAS_BLE_HID

class AsyncWebServerRequest;

void handlePostBlePairingStart(AsyncWebServerRequest* request);

#endif // HAS_BLE_HID
