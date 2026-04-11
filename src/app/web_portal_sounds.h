#pragma once

#include "board_config.h"

#if HAS_SOUND_PLAYER

#include <ESPAsyncWebServer.h>

// POST /api/sounds/upload?name=<name> — upload MP3 sound file (body handler)
void handlePostSoundUpload(AsyncWebServerRequest *request, uint8_t *data,
                           size_t len, size_t index, size_t total);

// GET /api/sounds/list — list all sound file names
void handleGetSoundList(AsyncWebServerRequest *request);

// DELETE /api/sounds?name=<name> — delete a sound file
void handleDeleteSound(AsyncWebServerRequest *request);

// POST /api/sounds/play?name=<name> — play a sound file (for testing)
void handlePostSoundPlay(AsyncWebServerRequest *request);

#endif // HAS_SOUND_PLAYER
