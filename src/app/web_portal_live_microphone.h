#pragma once

#include "board_config.h"

#if HAS_MICROPHONE_STREAMING
#include <ESPAsyncWebServer.h>

void handleLiveMicrophoneStream(AsyncWebServerRequest* request);
#endif
