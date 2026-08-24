#pragma once

#include "board_config.h"

#if HAS_CAMERA

#include <ESPAsyncWebServer.h>

void handleGetCameraRawSnapshot(AsyncWebServerRequest* request);
void handleGetCameraJpegSnapshot(AsyncWebServerRequest* request);
void handleGetCameraMjpegStream(AsyncWebServerRequest* request);

#endif