#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include <ESPAsyncWebServer.h>

// POST /api/icons/install?id=<id> — install PNG icon (body handler)
void handlePostIconInstall(AsyncWebServerRequest *request, uint8_t *data,
                           size_t len, size_t index, size_t total);

// DELETE /api/icons/page?page=N — delete all icons for a page
void handleDeletePageIcons(AsyncWebServerRequest *request);

// GET /api/icons/installed — list installed icon IDs
void handleGetInstalledIcons(AsyncWebServerRequest *request);

// GET /api/pad/button_sizes?cols=N&rows=N — return button dimensions for layout
void handleGetButtonSizes(AsyncWebServerRequest *request);

// Debug endpoints
// GET /api/icons/files — list all files in /icons/ with full name + size
void handleGetIconFiles(AsyncWebServerRequest *request);

// GET /api/icons/cache — dump cache entries (id, kind, w, h, data_size)
void handleGetIconCache(AsyncWebServerRequest *request);

// GET /api/icons/file?name=<filename> — download raw file from /icons/
void handleGetIconFile(AsyncWebServerRequest *request);

// DELETE /api/icons/file?name=<filename> — delete a specific file from /icons/
void handleDeleteIconFile(AsyncWebServerRequest *request);

#endif // HAS_DISPLAY
