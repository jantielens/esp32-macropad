#pragma once

#include <ESPAsyncWebServer.h>

// Origin used for CORS allowlist (scheme + host).
const char* web_portal_cors_origin();

// Attach CORS headers as default headers (global).
void web_portal_add_default_cors_headers();

// Attach CORS headers to a response.
void web_portal_add_cors_headers(AsyncWebServerResponse* response);

// Send a preflight response with CORS headers.
void web_portal_send_cors_preflight(AsyncWebServerRequest* request);
