#pragma once

#include "board_config.h"

#if IS_SHUTTER_TESTER

#include <ESPAsyncWebServer.h>

// GET  /api/sessions              — list all sessions (manifest)
// GET  /api/sessions/{id}         — stream full session JSON from LittleFS
// DELETE /api/sessions/{id}       — delete a session
// PATCH  /api/sessions/{id}       — update camera, notes

void web_portal_sessions_register_routes(AsyncWebServer* server);

#endif // IS_SHUTTER_TESTER
