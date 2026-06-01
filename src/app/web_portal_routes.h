#ifndef WEB_PORTAL_ROUTES_H
#define WEB_PORTAL_ROUTES_H

#include <ESPAsyncWebServer.h>

// Register all page, asset, and API routes for the web portal.
// Note: Keep this function focused on wiring routes only (no side effects).
void web_portal_register_routes(AsyncWebServer* server);

// ============================================================================
// Auto-registered route initializers
// ============================================================================
//
// Feature modules (especially device-class web routes) self-register their
// AsyncWebServer routes via REGISTER_ROUTES(). Each registered initializer is
// invoked once from web_portal_register_routes() before the generic component
// API routes, preserving the original ordering (exact-match routes ahead of
// the wildcard component dispatcher).
//
// Usage in a .cpp file:
//
//   static void shutter_sessions_register(AsyncWebServer* server) { ... }
//   REGISTER_ROUTES(shutter_sessions_register);
//
// The .cpp file must be picked up by an aggregation translation unit
// (route_components.cpp at the sketch root) so the static initializer runs.

typedef void (*RouteInitFn)(AsyncWebServer* server);

// Append a route initializer to the registry. Returns false if the registry is
// full (silently dropped).
bool web_portal_routes_add(RouteInitFn fn);

// Invoke every registered route initializer against the given server.
// Called from web_portal_register_routes().
void web_portal_routes_register_all(AsyncWebServer* server);

#define REGISTER_ROUTES(fn) \
    static struct _RouteRegistrar_##fn { \
        _RouteRegistrar_##fn() { web_portal_routes_add(&fn); } \
    } _route_registrar_##fn;

#endif // WEB_PORTAL_ROUTES_H
