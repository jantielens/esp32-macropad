#include "web_portal_ap.h"

#include "board_config.h"
#include "log_manager.h"
#include "class_branding.h"
#include "config_manager.h"
#include "wifi_manager.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <string.h>
#include <atomic>
#include <freertos/portmacro.h>

// AP configuration
static constexpr uint16_t DNS_PORT = 53;
static const IPAddress CAPTIVE_PORTAL_IP(192, 168, 4, 1);

static DNSServer dnsServer;
enum class ApPurpose : uint8_t { Off, Recovery, Interactive };
static std::atomic<ApPurpose> ap_purpose{ApPurpose::Off};
static portMUX_TYPE ap_info_mux = portMUX_INITIALIZER_UNLOCKED;
static char active_ap_name[33];
static char active_ap_password[CONFIG_AP_PASSWORD_MAX_LEN];
static char active_ap_ip[16];

bool web_portal_is_ap_mode() {
		return ap_purpose.load(std::memory_order_acquire) != ApPurpose::Off;
}

bool web_portal_is_recovery_ap() {
		return ap_purpose.load(std::memory_order_acquire) == ApPurpose::Recovery;
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

bool web_portal_start_ap(const DeviceConfig* config, bool full_portal) {
		if (!config) {
				LOGE("AP", "Cannot start AP without configuration");
				return false;
		}
		if (!wifi_ap_password_is_valid(config->ap_password)) {
				LOGE("AP", "Cannot start AP: configured password is invalid");
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
				LOGE("AP", "Failed to configure AP address");
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
				LOGE("AP", "Failed to start AP");
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
		const String ip = WiFi.softAPIP().toString();
		portENTER_CRITICAL(&ap_info_mux);
		strlcpy(active_ap_name, apName, sizeof(active_ap_name));
		strlcpy(active_ap_password, config->ap_password, sizeof(active_ap_password));
		strlcpy(active_ap_ip, ip.c_str(), sizeof(active_ap_ip));

		// Mark AP mode active so watchdog/DNS handling knows we're in captive portal
		ap_purpose.store(full_portal ? ApPurpose::Interactive : ApPurpose::Recovery,
				std::memory_order_release);
		portEXIT_CRITICAL(&ap_info_mux);

		LOGI("AP", "SSID: %s", active_ap_name);
		LOGI("AP", "IP: %s", active_ap_ip);
		LOGI("AP", "Captive portal active");
		return true;
}

void web_portal_stop_ap() {
		if (!web_portal_is_ap_mode()) return;

		LOGI("Portal", "Stopping AP mode");
		dnsServer.stop();
		WiFi.softAPdisconnect(true);
		portENTER_CRITICAL(&ap_info_mux);
		ap_purpose.store(ApPurpose::Off, std::memory_order_release);
		active_ap_name[0] = '\0';
		active_ap_password[0] = '\0';
		active_ap_ip[0] = '\0';
		portEXIT_CRITICAL(&ap_info_mux);
}

bool web_portal_ap_get_setup_info(char* ssid, size_t ssid_len,
		char* password, size_t password_len, char* ip, size_t ip_len) {
		if (!ssid || !ssid_len || !password || !password_len || !ip || !ip_len) {
				return false;
		}
		portENTER_CRITICAL(&ap_info_mux);
		if (!web_portal_is_ap_mode()) {
				portEXIT_CRITICAL(&ap_info_mux);
				return false;
		}
		strlcpy(ssid, active_ap_name, ssid_len);
		strlcpy(password, active_ap_password, password_len);
		strlcpy(ip, active_ap_ip, ip_len);
		portEXIT_CRITICAL(&ap_info_mux);
		return true;
}

bool web_portal_ap_get_network_info(char* ssid, size_t ssid_len, char* ip, size_t ip_len) {
		if (!ssid || !ssid_len || !ip || !ip_len) return false;
		portENTER_CRITICAL(&ap_info_mux);
		const bool active = web_portal_is_ap_mode();
		strlcpy(ssid, active ? active_ap_name : "", ssid_len);
		strlcpy(ip, active ? active_ap_ip : "", ip_len);
		portEXIT_CRITICAL(&ap_info_mux);
		return active;
}

void web_portal_ap_handle() {
		if (web_portal_is_ap_mode() && !wifi_manager_network_transition_pending()) {
				dnsServer.processNextRequest();
		}
}
