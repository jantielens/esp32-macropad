#pragma once

#include "board_config.h"

#if HAS_SOUND_PLAYER

#include <ESPAsyncWebServer.h>

// GET /api/music — return the audio worker's read-only music catalog snapshot.
void handleGetMusicCatalog(AsyncWebServerRequest* request);

#endif // HAS_SOUND_PLAYER