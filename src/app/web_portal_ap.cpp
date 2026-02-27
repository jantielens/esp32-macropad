#include "web_portal_ap.h"

#include "log_manager.h"
#include "project_branding.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>

// AP configuration
static constexpr uint16_t DNS_PORT = 53;
static const IPAddress CAPTIVE_PORTAL_IP(192, 168, 4, 1);

static DNSServer dnsServer;
static bool ap_mode_active = false;

bool web_portal_is_ap_mode() {
		return ap_mode_active;
}

void web_portal_ap_register_not_found(AsyncWebServer *server) {
		if (!server) return;

		server->onNotFound([](AsyncWebServerRequest *request) {
				// In AP mode, redirect to root for captive portal
				if (web_portal_is_ap_mode()) {
						request->redirect("/");
				} else {
						request->send(404, "text/plain", "Not found");
				}
		});
}

void web_portal_start_ap() {
		LOGI("AP", "Mode start");

		// Generate AP name with chip ID
		uint32_t chipId = 0;
		for (int i = 0; i < 17; i = i + 8) {
				chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
		}

		// Convert PROJECT_NAME to uppercase for AP SSID
		String apPrefix = String(PROJECT_NAME);
		apPrefix.toUpperCase();
		String apName = apPrefix + "-" + String(chipId, HEX);

		LOGI("AP", "SSID: %s", apName.c_str());

		// Configure AP
		WiFi.mode(WIFI_AP);
		WiFi.softAPConfig(CAPTIVE_PORTAL_IP, CAPTIVE_PORTAL_IP, IPAddress(255, 255, 255, 0));
		WiFi.softAP(apName.c_str());

		// Start DNS server for captive portal (redirect all DNS queries to our IP)
		dnsServer.start(DNS_PORT, "*", CAPTIVE_PORTAL_IP);

		WiFi.softAPsetHostname(apName.c_str());

		// Mark AP mode active so watchdog/DNS handling knows we're in captive portal
		ap_mode_active = true;

		LOGI("AP", "IP: %s", WiFi.softAPIP().toString().c_str());
		LOGI("AP", "Captive portal active");
}

void web_portal_stop_ap() {
		if (!ap_mode_active) return;

		LOGI("Portal", "Stopping AP mode");
		dnsServer.stop();
		WiFi.softAPdisconnect(true);
		ap_mode_active = false;
}

void web_portal_ap_handle() {
		if (ap_mode_active) {
				dnsServer.processNextRequest();
		}
}
