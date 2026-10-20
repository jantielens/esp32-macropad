#ifndef WEB_PORTAL_AP_H
#define WEB_PORTAL_AP_H

#include <ESPAsyncWebServer.h>
#include <stddef.h>

struct DeviceConfig;

// Captive portal (AP mode) server behavior.
void web_portal_ap_register_not_found(AsyncWebServer *server);

// Public portal APIs implemented here.
bool web_portal_start_ap(const DeviceConfig* config);
void web_portal_stop_ap();
bool web_portal_is_ap_mode();
bool web_portal_ap_get_setup_info(char* ssid, size_t ssid_len,
		char* password, size_t password_len, char* ip, size_t ip_len);

// Loop-time AP DNS processing.
void web_portal_ap_handle();

#endif // WEB_PORTAL_AP_H
