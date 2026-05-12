#pragma once

#include <ESPAsyncWebServer.h>

#ifndef HTTP_STREAM_CHUNK_SIZE
#define HTTP_STREAM_CHUNK_SIZE 4096
#endif

// Stream a LittleFS file via AwsResponseFiller callback.
//
// Uses Content-Length (not chunked transfer-encoding) so browsers get a
// progress indicator.  The async TCP event loop yields between filler
// invocations, preventing WiFi TX buffer exhaustion on ESP-Hosted (SDIO).
void sendFileThrottled(AsyncWebServerRequest *request,
                       const char *path,
                       const char *content_type);
