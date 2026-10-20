#include "web_portal_ap.h"

#include "board_config.h"
#include "log_manager.h"
#include "project_branding.h"
#include "class_branding.h"
#include "config_manager.h"
#include "wifi_manager.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <string.h>

// AP configuration
static constexpr uint16_t DNS_PORT = 53;
static const IPAddress CAPTIVE_PORTAL_IP(192, 168, 4, 1);

static DNSServer dnsServer;
static bool ap_mode_active = false;
static char active_ap_name[33];
static char active_ap_password[CONFIG_AP_PASSWORD_MAX_LEN];
static char active_ap_ip[16];

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

bool web_portal_start_ap(const DeviceConfig* config) {
		if (!config) {
				LOGE("AP", "Cannot start setup AP without configuration");
				return false;
		}
		if (!wifi_ap_password_is_valid(config->ap_password)) {
				LOGE("AP", "Cannot start setup AP: configured password is invalid");
				return false;
		}

		LOGI("AP", "Mode start");

		// Generate AP name from chip ID. Device-class-aware SSID:
		//   Macropad: ESP32-MACROPAD-{XXXXXX}
		//   E-Paper:  ESP32-MP-EPAPER-{XXXXXX}
		//   Headless: ESP32-MP-HEADLESS-{XXXXXX}
		// where {XXXXXX} is the low 24 bits of the eFuse chip id formatted
		// as fixed-width uppercase hex.
		uint32_t chipId = 0;
		for (int i = 0; i < 17; i = i + 8) {
				chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
		}
		uint32_t chipIdSuffix = chipId & 0xFFFFFF;  // last 24 bits = 6 hex digits

		char apName[33];  // SSID max 32 chars + NUL
		const char *classSlug = device_class_get_slug();
		if (strcmp(classSlug, "MACROPAD") == 0) {
				snprintf(apName, sizeof(apName), "ESP32-MACROPAD-%06X", chipIdSuffix);
		} else {
				snprintf(apName, sizeof(apName), "ESP32-MP-%s-%06X", classSlug, chipIdSuffix);
		}

		if (!WiFi.mode(WIFI_AP)) {
				LOGE("AP", "Failed to select AP radio mode");
				return false;
		}
		if (!WiFi.softAPConfig(CAPTIVE_PORTAL_IP, CAPTIVE_PORTAL_IP, IPAddress(255, 255, 255, 0))) {
				LOGE("AP", "Failed to configure setup AP address");
				WiFi.mode(WIFI_OFF);
				return false;
		}
		const char* passphrase = config->ap_password[0] ? config->ap_password : nullptr;
#ifdef AP_MAX_CONNECTIONS
		// Board override: cap concurrent stations to save DMA-internal SRAM
		// (relevant on small-RAM SoCs like ESP32-C3).
		const bool started = WiFi.softAP(apName, passphrase, /*channel=*/1, /*hidden=*/0,
		            /*max_connection=*/AP_MAX_CONNECTIONS);
#else
		const bool started = WiFi.softAP(apName, passphrase);
#endif
		if (!started) {
				LOGE("AP", "Failed to start setup AP");
				WiFi.softAPdisconnect(true);
				WiFi.mode(WIFI_OFF);
				return false;
		}

		// Start DNS server for captive portal (redirect all DNS queries to our IP)
		if (!dnsServer.start(DNS_PORT, "*", CAPTIVE_PORTAL_IP)) {
				LOGE("AP", "Failed to start captive DNS");
				WiFi.softAPdisconnect(true);
				WiFi.mode(WIFI_OFF);
				return false;
		}

		WiFi.softAPsetHostname(apName);
		strlcpy(active_ap_name, apName, sizeof(active_ap_name));
		strlcpy(active_ap_password, config->ap_password, sizeof(active_ap_password));
		const String ip = WiFi.softAPIP().toString();
		strlcpy(active_ap_ip, ip.c_str(), sizeof(active_ap_ip));

		// Mark AP mode active so watchdog/DNS handling knows we're in captive portal
		ap_mode_active = true;

		LOGI("AP", "SSID: %s", active_ap_name);
		LOGI("AP", "IP: %s", active_ap_ip);
		LOGI("AP", "Captive portal active");
		return true;
}

void web_portal_stop_ap() {
		if (!ap_mode_active) return;

		LOGI("Portal", "Stopping AP mode");
		dnsServer.stop();
		WiFi.softAPdisconnect(true);
		ap_mode_active = false;
		active_ap_name[0] = '\0';
		active_ap_password[0] = '\0';
		active_ap_ip[0] = '\0';
}

bool web_portal_ap_get_setup_info(char* ssid, size_t ssid_len,
		char* password, size_t password_len, char* ip, size_t ip_len) {
		if (!ap_mode_active || !ssid || !ssid_len || !password || !password_len || !ip || !ip_len) {
				return false;
		}
		strlcpy(ssid, active_ap_name, ssid_len);
		strlcpy(password, active_ap_password, password_len);
		strlcpy(ip, active_ap_ip, ip_len);
		return true;
}

void web_portal_ap_handle() {
		if (ap_mode_active) {
				dnsServer.processNextRequest();
		}
}
