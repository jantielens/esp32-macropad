#pragma once

#include "board_config.h"
#if HAS_REMOTE_LOG
#include <ESPAsyncWebServer.h>

bool portal_logs_full_mode_enabled();
void handleGetLogs(AsyncWebServerRequest* request);
void handleGetCrashLog(AsyncWebServerRequest* request);
void handleDownloadCrashLog(AsyncWebServerRequest* request);
#if DEBUG_CRASH_API_ENABLED
void handleDebugCrash(AsyncWebServerRequest* request);
#endif
#endif