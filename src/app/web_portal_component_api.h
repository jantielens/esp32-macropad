#pragma once

#include <ESPAsyncWebServer.h>

// Register generic component API routes on the server.
// MUST be called AFTER all specific routes are registered (first-match routing).
void web_portal_register_component_routes(AsyncWebServer* server);
