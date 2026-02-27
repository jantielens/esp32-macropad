#ifndef WEB_PORTAL_ROUTES_H
#define WEB_PORTAL_ROUTES_H

#include <ESPAsyncWebServer.h>

// Register all page, asset, and API routes for the web portal.
// Note: Keep this function focused on wiring routes only (no side effects).
void web_portal_register_routes(AsyncWebServer* server);

#endif // WEB_PORTAL_ROUTES_H
