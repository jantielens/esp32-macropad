#pragma once

#include <ESPAsyncWebServer.h>

bool portal_logs_full_mode_enabled();
void handleGetLogs(AsyncWebServerRequest* request);
void handleGetCrashLog(AsyncWebServerRequest* request);
void handleDownloadCrashLog(AsyncWebServerRequest* request);
void handleDebugCrash(AsyncWebServerRequest* request);