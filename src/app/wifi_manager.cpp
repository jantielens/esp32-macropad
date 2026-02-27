#include "wifi_manager.h"

#include "board_config.h"
#include "config_manager.h"
#include "log_manager.h"
#include "power_manager.h"
#include "../version.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <lwip/netif.h>

// WiFi retry settings
static constexpr unsigned long WIFI_BACKOFF_BASE = 3000; // 3 seconds base
static constexpr unsigned long WIFI_CHECK_INTERVAL_MS = 10000; // 10 seconds

static unsigned long g_last_wifi_check_ms = 0;

RTC_DATA_ATTR static uint8_t g_cached_bssid[6] = {0};
RTC_DATA_ATTR static uint8_t g_cached_channel = 0;
RTC_DATA_ATTR static bool g_cached_valid = false;
RTC_DATA_ATTR static char g_cached_ssid[CONFIG_SSID_MAX_LEN] = {0};

static void format_bssid(const uint8_t *bssid, char *out, size_t out_len) {
		if (!out || out_len < 18) return;
		if (!bssid) {
				snprintf(out, out_len, "--:--:--:--:--:--");
				return;
		}
		snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
				bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

static bool wait_for_connection(unsigned long timeout_ms) {
		const unsigned long start = millis();
		while (millis() - start < timeout_ms) {
				if (WiFi.status() == WL_CONNECTED) {
						return true;
				}
				delay(100);
		}
		return false;
}

static bool select_strongest_ap(const char *target_ssid, uint8_t out_bssid[6], int *out_channel, int *out_rssi) {
		if (!target_ssid || strlen(target_ssid) == 0) return false;

		WiFi.scanDelete();

		LOGI("WiFi", "Scan start");
		const int16_t n = WiFi.scanNetworks();
		if (n < 0) {
				LOGW("WiFi", "Scan failed");
				return false;
		}

		int best_index = -1;
		int best_rssi = -1000;
		int matches = 0;

		for (int i = 0; i < n; i++) {
				if (WiFi.SSID(i) == target_ssid) {
						matches++;
						const int rssi = WiFi.RSSI(i);
						if (best_index < 0 || rssi > best_rssi) {
								best_index = i;
								best_rssi = rssi;
						}
				}
		}

		LOGI("WiFi", "Found %d networks (%d matching SSID)", (int)n, matches);

		if (best_index < 0) {
				LOGW("WiFi", "No matching SSID");
				WiFi.scanDelete();
				return false;
		}

		const uint8_t *best_bssid_ptr = WiFi.BSSID(best_index);
		const int best_channel = WiFi.channel(best_index);

		if (!best_bssid_ptr || best_channel <= 0) {
				LOGW("WiFi", "Missing BSSID/channel");
				WiFi.scanDelete();
				return false;
		}

		memcpy(out_bssid, best_bssid_ptr, 6);
		if (out_channel) *out_channel = best_channel;
		if (out_rssi) *out_rssi = best_rssi;

		char bssid_str[18];
		format_bssid(out_bssid, bssid_str, sizeof(bssid_str));
		LOGI("WiFi", "Selected AP: %s | Ch %d | RSSI %d dBm", bssid_str, best_channel, best_rssi);

		WiFi.scanDelete();
		return true;
}

bool wifi_manager_connect(const DeviceConfig *config, bool allow_cached_bssid) {
		if (!config) return false;

		LOGI("WiFi", "Connection start");
		LOGI("WiFi", "SSID: %s", config->wifi_ssid);

		if (strlen(config->wifi_ssid) == 0) {
				LOGW("WiFi", "SSID not set");
				return false;
		}

		WiFi.persistent(false);

		#ifdef CONFIG_IDF_TARGET_ESP32P4
		// ESP32-P4 uses an external ESP32-C6 co-processor over SDIO (ESP-Hosted).
		// The SDIO link takes several seconds to establish after boot. The
		// aggressive disconnect/WIFI_OFF cycle used for normal ESP32 chips
		// disrupts the SDIO transport negotiation and corrupts internal state,
		// causing subsequent WiFi.begin() to fail even though scans succeed.
		// Instead, just set STA mode and let the hosted link come up cleanly.
		WiFi.mode(WIFI_STA);
		LOGI("WiFi", "Waiting for ESP-Hosted link...");
		delay(5000);
		#else
		WiFi.disconnect(true);
		delay(100);
		WiFi.mode(WIFI_OFF);
		delay(500);
		WiFi.mode(WIFI_STA);
		delay(100);
		#endif

		// ESP32-P4 ESP-Hosted: setSleep(false) is proxied over SDIO and can
		// destabilise the hosted link during early init. Skip it on P4.
		#ifndef CONFIG_IDF_TARGET_ESP32P4
		WiFi.setSleep(false);
		#endif
		WiFi.setAutoReconnect(true);

		char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
		config_manager_sanitize_device_name(config->device_name, sanitized, CONFIG_DEVICE_NAME_MAX_LEN);

		if (strlen(sanitized) > 0) {
				WiFi.setHostname(sanitized);
				LOGI("WiFi", "Hostname: %s", sanitized);

				esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
				if (netif != NULL) {
						esp_netif_set_hostname(netif, sanitized);
				}
		}

		if (strlen(config->fixed_ip) > 0) {
				LOGI("WiFi", "Fixed IP config start");

				IPAddress local_ip, gateway, subnet, dns1, dns2;

				if (!local_ip.fromString(config->fixed_ip)) {
						LOGE("WiFi", "Invalid IP address");
						LOGE("WiFi", "Connection failed");
						return false;
				}

				if (!subnet.fromString(config->subnet_mask)) {
						LOGE("WiFi", "Invalid subnet mask");
						LOGE("WiFi", "Connection failed");
						return false;
				}

				if (!gateway.fromString(config->gateway)) {
						LOGE("WiFi", "Invalid gateway");
						LOGE("WiFi", "Connection failed");
						return false;
				}

				if (strlen(config->dns1) > 0) {
						dns1.fromString(config->dns1);
				} else {
						dns1 = gateway;
				}

				if (strlen(config->dns2) > 0) {
						dns2.fromString(config->dns2);
				} else {
						dns2 = IPAddress(0, 0, 0, 0);
				}

				if (!WiFi.config(local_ip, gateway, subnet, dns1, dns2)) {
						LOGE("WiFi", "Configuration failed");
						LOGE("WiFi", "Connection failed");
						return false;
				}

				LOGI("WiFi", "IP: %s", config->fixed_ip);
		}

		if (allow_cached_bssid && g_cached_valid && g_cached_channel > 0) {
				if (strncmp(g_cached_ssid, config->wifi_ssid, sizeof(g_cached_ssid)) != 0) {
						g_cached_valid = false;
				}
		}

		if (allow_cached_bssid && g_cached_valid && g_cached_channel > 0) {
				char bssid_str[18];
				format_bssid(g_cached_bssid, bssid_str, sizeof(bssid_str));
				LOGI("WiFi", "Using cached AP: %s | Ch %u", bssid_str, (unsigned)g_cached_channel);

				WiFi.begin(config->wifi_ssid, config->wifi_password, g_cached_channel, g_cached_bssid);
				if (wait_for_connection(3000)) {
						LOGI("WiFi", "Connected (cached AP)");
						return true;
				}

				LOGW("WiFi", "Cached AP failed; scanning");
		}

		#ifdef CONFIG_IDF_TARGET_ESP32P4
		// ESP32-P4 ESP-Hosted: the C6 co-processor handles AP selection
		// internally. Scanning + directed-connect (BSSID/channel) over SDIO
		// causes ASSOC_LEAVE (reason 8). Use simple begin and let C6 decide.
		// Also, auto-reconnect doesn't propagate reliably over SDIO, so we
		// re-issue WiFi.begin() on each attempt with a clean disconnect cycle.
		for (int attempt = 0; attempt < WIFI_MAX_ATTEMPTS; attempt++) {
				if (attempt > 0) {
						WiFi.disconnect(false);
						delay(2000);
				}
				LOGI("WiFi", "Attempt %d/%d", attempt + 1, WIFI_MAX_ATTEMPTS);
				WiFi.begin(config->wifi_ssid, config->wifi_password);

				unsigned long timeout = WIFI_BACKOFF_BASE * (attempt + 1);
				if (wait_for_connection(timeout)) {
						LOGI("WiFi", "IP: %s", WiFi.localIP().toString().c_str());
						LOGI("WiFi", "Hostname: %s", WiFi.getHostname());
						LOGI("WiFi", "MAC: %s", WiFi.macAddress().c_str());
						LOGI("WiFi", "Signal: %d dBm", WiFi.RSSI());
						LOGI("WiFi", "Access: http://%s", WiFi.localIP().toString().c_str());
						LOGI("WiFi", "Access: http://%s.local", WiFi.getHostname());
						LOGI("WiFi", "Connected");
						return true;
				}

				wl_status_t status = WiFi.status();
				const char* reason =
						(status == WL_NO_SSID_AVAIL) ? "SSID not found" :
						(status == WL_CONNECT_FAILED) ? "Connect failed (wrong password?)" :
						(status == WL_CONNECTION_LOST) ? "Connection lost" :
						(status == WL_DISCONNECTED) ? "Disconnected" :
						"Unknown";
				LOGW("WiFi", "Status: %s (%d)", reason, status);
		}
		#else
		uint8_t best_bssid[6];
		int best_channel = 0;
		int best_rssi = 0;
		const bool has_best_ap = select_strongest_ap(config->wifi_ssid, best_bssid, &best_channel, &best_rssi);
		if (has_best_ap) {
				WiFi.begin(config->wifi_ssid, config->wifi_password, best_channel, best_bssid);
		} else {
				WiFi.begin(config->wifi_ssid, config->wifi_password);
		}

		for (int attempt = 0; attempt < WIFI_MAX_ATTEMPTS; attempt++) {
				unsigned long backoff = WIFI_BACKOFF_BASE * (attempt + 1);
				unsigned long start = millis();

				LOGI("WiFi", "Attempt %d/%d (timeout %ds)", attempt + 1, WIFI_MAX_ATTEMPTS, backoff / 1000);

				while (millis() - start < backoff) {
						if (WiFi.status() == WL_CONNECTED) {
								LOGI("WiFi", "IP: %s", WiFi.localIP().toString().c_str());
								LOGI("WiFi", "Hostname: %s", WiFi.getHostname());
								LOGI("WiFi", "MAC: %s", WiFi.macAddress().c_str());
								LOGI("WiFi", "Signal: %d dBm", WiFi.RSSI());
								LOGI("WiFi", "Access: http://%s", WiFi.localIP().toString().c_str());
								LOGI("WiFi", "Access: http://%s.local", WiFi.getHostname());
								LOGI("WiFi", "Connected");

								if (has_best_ap && best_channel > 0) {
										memcpy(g_cached_bssid, best_bssid, sizeof(g_cached_bssid));
										g_cached_channel = (uint8_t)best_channel;
										g_cached_valid = true;
										strlcpy(g_cached_ssid, config->wifi_ssid, sizeof(g_cached_ssid));
								}

								return true;
						}
						delay(100);
				}

				wl_status_t status = WiFi.status();
				if (status != WL_CONNECTED) {
						const char* reason =
								(status == WL_NO_SSID_AVAIL) ? "SSID not found" :
								(status == WL_CONNECT_FAILED) ? "Connect failed (wrong password?)" :
								(status == WL_CONNECTION_LOST) ? "Connection lost" :
								(status == WL_DISCONNECTED) ? "Disconnected" :
								"Unknown";
						LOGW("WiFi", "Status: %s (%d)", reason, status);
				}
		}
		#endif

		LOGE("WiFi", "All attempts failed");
		return false;
}

void wifi_manager_start_mdns(const DeviceConfig *config) {
		if (!config) return;

		LOGI("mDNS", "Start");

		char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
		config_manager_sanitize_device_name(config->device_name, sanitized, CONFIG_DEVICE_NAME_MAX_LEN);

		if (strlen(sanitized) == 0) {
				LOGE("mDNS", "Empty hostname");
				return;
		}

		if (MDNS.begin(sanitized)) {
				LOGI("mDNS", "Name: %s.local", sanitized);

				MDNS.addService("http", "tcp", 80);

				MDNS.addServiceTxt("http", "tcp", "version", FIRMWARE_VERSION);
				MDNS.addServiceTxt("http", "tcp", "model", ESP.getChipModel());

				String mac = WiFi.macAddress();
				mac.replace(":", "");
				String mac_short = mac.substring(mac.length() - 4);
				MDNS.addServiceTxt("http", "tcp", "mac", mac_short.c_str());

				MDNS.addServiceTxt("http", "tcp", "ty", "iot-device");
				MDNS.addServiceTxt("http", "tcp", "mf", "ESP32-Tmpl");

				MDNS.addServiceTxt("http", "tcp", "features", "wifi,http,api");

				String config_url = "http://";
				config_url += sanitized;
				config_url += ".local";
				MDNS.addServiceTxt("http", "tcp", "url", config_url.c_str());

				LOGI("mDNS", "TXT records: version, model, mac, ty, features");
		} else {
				LOGE("mDNS", "Failed to start");
		}
}

void wifi_manager_watchdog(const DeviceConfig *config, bool config_loaded, bool is_ap_mode) {
		if (!config || !config_loaded || is_ap_mode) return;

		const unsigned long now = millis();
		if (now - g_last_wifi_check_ms < WIFI_CHECK_INTERVAL_MS) return;

		if (WiFi.status() != WL_CONNECTED && strlen(config->wifi_ssid) > 0) {
				LOGW("WIFI", "Watchdog: connection lost - attempting reconnect");
				if (wifi_manager_connect(config, false)) {
						power_manager_note_wifi_success();
						wifi_manager_start_mdns(config);
				}
		}

		g_last_wifi_check_ms = now;
}
