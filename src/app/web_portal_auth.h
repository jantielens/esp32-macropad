#ifndef WEB_PORTAL_AUTH_H
#define WEB_PORTAL_AUTH_H

#include <ESPAsyncWebServer.h>

// Basic auth gate (optional; STA/full mode only).
// Returns true if request is authorized (or auth disabled); otherwise sends auth challenge and returns false.
bool portal_auth_gate(AsyncWebServerRequest *request);

#endif // WEB_PORTAL_AUTH_H
