#ifndef WEB_PORTAL_AP_H
#define WEB_PORTAL_AP_H

#include <ESPAsyncWebServer.h>

// Captive portal (AP mode) server behavior.
void web_portal_ap_register_not_found(AsyncWebServer *server);

// Public portal APIs implemented here.
void web_portal_start_ap();
void web_portal_stop_ap();
bool web_portal_is_ap_mode();

// Loop-time AP DNS processing.
void web_portal_ap_handle();

#endif // WEB_PORTAL_AP_H
