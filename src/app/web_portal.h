/*
 * Web Configuration Portal
 * 
 * Provides web-based configuration interface for device settings.
 * Runs in AP mode for initial setup or after config reset.
 * Remains accessible at device IP after WiFi connection.
 * 
 * FEATURES:
 * - Captive portal (DNS catch-all)
 * - Async web server (non-blocking)
 * - REST API for config management
 * - Static asset serving (HTML/CSS/JS)
 * - OTA firmware updates via web upload
 * - Core portal mode (AP) vs Full portal mode (WiFi connected)
 * 
 * ENDPOINTS:
 *   GET  /              -> Portal HTML page
 *   GET  /portal.css    -> CSS stylesheet
 *   GET  /portal.js     -> JavaScript
 *   GET  /api/config    -> Get current config (JSON)
 *   POST /api/config    -> Save new config (JSON)
 *   DELETE /api/config  -> Reset config
 *   GET  /api/version   -> Get firmware version (JSON)
 *   GET  /api/mode      -> Get portal mode (core vs full)
 *   POST /api/update    -> Upload firmware binary for OTA update
 */

#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>
#include "config_manager.h"

// API Functions
void web_portal_init(DeviceConfig *config);        // Initialize web server and DNS
void web_portal_start_ap();                        // Start AP mode with captive portal (core mode)
void web_portal_stop_ap();                         // Stop AP mode
void web_portal_handle();                          // Handle web server (call in loop)
bool web_portal_is_ap_mode();                      // Check if in AP mode
bool web_portal_ota_in_progress();                 // Check if OTA update is in progress

#endif // WEB_PORTAL_H
