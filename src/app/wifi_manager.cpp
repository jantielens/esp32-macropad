#include "wifi_manager.h"

#include "board_config.h"
#include "config_manager.h"
#include "device_telemetry.h"
#include "log_manager.h"
#include "power_manager.h"
#include "../version.h"

#include <atomic>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <lwip/netif.h>
#include "ping/ping_sock.h"

// Early init state
static bool g_early_init_done = false;

// WiFi retry settings (boot-time only)
static constexpr unsigned long WIFI_BACKOFF_BASE = 3000; // 3 seconds base
static constexpr unsigned long WIFI_CHECK_INTERVAL_MS = 10000; // 10 seconds (safety-net poll)
static constexpr unsigned long WIFI_PING_INTERVAL_MS  = 30000; // 30 seconds between pings
static constexpr int          WIFI_PING_FAIL_THRESHOLD = 3;    // consecutive failures before reconnect
static constexpr unsigned long WIFI_TIER3_REINIT_TIMEOUT_MS = 60000; // Tier 3 reinit retry interval

static unsigned long g_last_wifi_check_ms = 0;

// --- Event-driven reconnect state machine ---
enum class WifiState : uint8_t {
		Connected,
		Tier1Wait,    // SDK auto-reconnect window
		Tier2Retry,   // Active reconnect with exponential backoff
		Tier3Reset,   // Hard WiFi stack reset
		Tier3Off,     // WiFi.mode(WIFI_OFF) applied, waiting 1 s (non-P4 only)
		Tier3Reinit,  // WiFi.mode(WIFI_STA) + begin() issued, waiting
};

static WifiState g_wifi_state = WifiState::Connected;
static unsigned long g_outage_start_ms = 0;   // millis() at outage start
static unsigned long g_state_entry_ms = 0;    // millis() when current state was entered
static unsigned long g_last_retry_ms = 0;     // millis() of last Tier 2 reconnect attempt
static unsigned int  g_tier2_attempt = 0;     // Tier 2 backoff attempt counter
static bool g_events_registered = false;

// Atomic flags — written from system event task, read from loop() task.
static std::atomic<bool> g_wifi_disconnected{false};
static std::atomic<bool> g_wifi_reconnected{false};

// --- Gateway ping liveness check ---
static unsigned long g_last_ping_ms = 0;
static int           g_ping_fail_count = 0;
static volatile bool g_ping_in_flight = false;
static volatile bool g_ping_got_reply = false;

static void ping_on_success(esp_ping_handle_t hdl, void *args) {
		(void)hdl; (void)args;
		g_ping_got_reply = true;
}

static void ping_on_timeout(esp_ping_handle_t hdl, void *args) {
		(void)hdl; (void)args;
		// g_ping_got_reply stays false
}

static void ping_on_end(esp_ping_handle_t hdl, void *args) {
		(void)args;
		esp_ping_delete_session(hdl);
		g_ping_in_flight = false;
}

static void wifi_ping_start(IPAddress gateway) {
		if (g_ping_in_flight) return;
		if (gateway == IPAddress(0, 0, 0, 0)) return;

		esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
		cfg.count = 1;
		cfg.timeout_ms = 2000;
		cfg.interval_ms = 0;
		cfg.data_size = 32;
		cfg.task_stack_size = 2048;
		cfg.task_prio = 1;  // lowest priority — never starve LVGL

		ip_addr_t target = {};
		IP_ADDR4(&target, gateway[0], gateway[1], gateway[2], gateway[3]);
		cfg.target_addr = target;

		esp_ping_callbacks_t cbs = {};
		cbs.on_ping_success = ping_on_success;
		cbs.on_ping_timeout = ping_on_timeout;
		cbs.on_ping_end     = ping_on_end;

		esp_ping_handle_t hdl = nullptr;
		g_ping_got_reply = false;
		g_ping_in_flight = true;

		esp_err_t err = esp_ping_new_session(&cfg, &cbs, &hdl);
		if (err != ESP_OK) {
				g_ping_in_flight = false;
				return;
		}
		esp_ping_start(hdl);
}

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

void wifi_manager_early_init() {
		if (g_early_init_done) return;
		g_early_init_done = true;

		WiFi.persistent(false);

		#ifdef CONFIG_IDF_TARGET_ESP32P4
		// ESP32-P4: start SDIO link to C6 co-processor early.
		// This takes 2-5 s; by calling it before display/config init the link
		// is usually ready by the time wifi_manager_connect() runs.
		WiFi.mode(WIFI_STA);
		LOGI("WiFi", "Early init: SDIO link starting");
		#else
		// Non-P4: clean WiFi state and enter STA mode.
		WiFi.disconnect(true);
		delay(100);
		WiFi.mode(WIFI_OFF);
		delay(500);
		WiFi.mode(WIFI_STA);
		delay(100);
		LOGI("WiFi", "Early init: STA mode ready");
		#endif
}

bool wifi_manager_connect(const DeviceConfig *config, bool allow_cached_bssid) {
		if (!config) return false;

		LOGI("WiFi", "Connection start");
		LOGI("WiFi", "SSID: %s", config->wifi_ssid);

		if (strlen(config->wifi_ssid) == 0) {
				LOGW("WiFi", "SSID not set");
				return false;
		}

		if (!g_early_init_done) {
				// Backward compat: if early_init wasn't called, do full hardware init now.
				wifi_manager_early_init();
		}

		#ifdef CONFIG_IDF_TARGET_ESP32P4
		// Wait for ESP-Hosted SDIO link if not yet ready.
		// If wifi_manager_early_init() was called earlier, the link may already
		// be established — this poll returns immediately in that case.
		LOGI("WiFi", "Waiting for ESP-Hosted link...");
		{
				const unsigned long hosted_start = millis();
				const unsigned long HOSTED_TIMEOUT_MS = 8000;
				bool link_ready = false;
				while (millis() - hosted_start < HOSTED_TIMEOUT_MS) {
						String mac = WiFi.macAddress();
						if (mac.length() > 0 && mac != "00:00:00:00:00:00") {
								link_ready = true;
								LOGI("WiFi", "ESP-Hosted link ready (%lums)", millis() - hosted_start);
								break;
						}
						delay(250);
				}
				if (!link_ready) {
						LOGW("WiFi", "ESP-Hosted link not confirmed after %lums, proceeding anyway", HOSTED_TIMEOUT_MS);
				}
				// Brief settle time after SDIO link is confirmed.
				// When wifi_manager_early_init() was called early, the link has
				// already been up for seconds — 100 ms is sufficient.
				delay(100);
		}
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
		// is unreliable and can cause ASSOC_LEAVE (reason 8), especially with
		// older C6 firmware. Use simple begin and let the C6 decide.
		// Auto-reconnect doesn't propagate reliably over SDIO, so we
		// re-issue WiFi.begin() on each attempt with a clean disconnect cycle.
		for (int attempt = 0; attempt < WIFI_MAX_ATTEMPTS; attempt++) {
				if (attempt > 0) {
						WiFi.disconnect(false);
						delay(500);
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

// --- WiFi event handlers (set flags only, no WiFi API calls) ---

static void on_wifi_disconnected(arduino_event_id_t event, arduino_event_info_t info) {
		(void)event; (void)info;
		g_wifi_disconnected.store(true, std::memory_order_release);
}

static void on_wifi_got_ip(arduino_event_id_t event, arduino_event_info_t info) {
		(void)event; (void)info;
		g_wifi_reconnected.store(true, std::memory_order_release);
}

void wifi_manager_register_events() {
		if (g_events_registered) return;
		g_events_registered = true;

		WiFi.onEvent(on_wifi_disconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
		WiFi.onEvent(on_wifi_got_ip, ARDUINO_EVENT_WIFI_STA_GOT_IP);
		LOGI("WiFi", "Event handlers registered");
}

// --- State machine helpers ---

static void enter_outage(unsigned long now) {
		g_outage_start_ms = now;
		g_state_entry_ms = now;
		g_last_retry_ms = 0;
		g_tier2_attempt = 0;
		g_wifi_state = WifiState::Tier1Wait;
		g_ping_fail_count = 0;
		LOGW("WiFi", "Disconnected — entering Tier 1 (SDK auto-reconnect)");
}

static void recover_from_outage(const DeviceConfig *config, unsigned long now,
                                const char *tier_name) {
		const unsigned long outage_duration = now - g_outage_start_ms;
		g_wifi_state = WifiState::Connected;
		g_wifi_disconnected.store(false, std::memory_order_release);
		g_wifi_reconnected.store(false, std::memory_order_release);

		power_manager_note_wifi_success();
		wifi_manager_start_mdns(config);
		device_telemetry_cache_rssi();

		LOGI("WiFi", "Reconnected from %s after %lus",
		     tier_name, outage_duration / 1000);
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
		if (strlen(config->wifi_ssid) == 0) return;

		const unsigned long now = millis();

		// --- Consume atomic event flags ---
		const bool event_disconnected = g_wifi_disconnected.exchange(false, std::memory_order_acquire);
		const bool event_reconnected = g_wifi_reconnected.exchange(false, std::memory_order_acquire);

		// --- Handle reconnection event (takes priority over disconnect) ---
		if (event_reconnected) {
				if (g_wifi_state != WifiState::Connected) {
						recover_from_outage(config, now,
								g_wifi_state == WifiState::Tier1Wait   ? "Tier 1" :
								g_wifi_state == WifiState::Tier2Retry  ? "Tier 2" : "Tier 3");
				}
				return; // Reconnected — ignore any simultaneous disconnect flag
		}

		// --- Handle disconnect event while connected ---
		if (event_disconnected && g_wifi_state == WifiState::Connected) {
				enter_outage(now);
				return;
		}

		// --- Safety-net polling (catches missed events) ---
		if (now - g_last_wifi_check_ms >= WIFI_CHECK_INTERVAL_MS) {
				g_last_wifi_check_ms = now;

				if (g_wifi_state == WifiState::Connected && WiFi.status() != WL_CONNECTED) {
						LOGW("WiFi", "Watchdog safety net: WiFi.status() not connected");
						enter_outage(now);
						return;
				}
				if (g_wifi_state != WifiState::Connected && WiFi.status() == WL_CONNECTED) {
						LOGW("WiFi", "Watchdog safety net: WiFi connected but state machine in outage");
						recover_from_outage(config, now, "safety-net");
						return;
				}
		}

		// --- Gateway ping liveness (only when connected) ---
		if (g_wifi_state == WifiState::Connected) {
				// Collect result from previous ping (if any)
				if (!g_ping_in_flight && g_last_ping_ms > 0) {
						if (g_ping_got_reply) {
								if (g_ping_fail_count > 0) {
										LOGI("WiFi", "Ping OK — link recovered after %d failure(s)", g_ping_fail_count);
								}
								g_ping_fail_count = 0;
						} else {
								g_ping_fail_count++;
								LOGW("WiFi", "Ping failed (%d/%d)", g_ping_fail_count, WIFI_PING_FAIL_THRESHOLD);

								if (g_ping_fail_count >= WIFI_PING_FAIL_THRESHOLD) {
										LOGW("WiFi", "Link dead (WiFi reported connected) — entering reconnect");
										g_ping_fail_count = 0;
										g_last_ping_ms = 0;
										WiFi.disconnect(false);
										enter_outage(now);
										return;
								}
						}
				}

				// Launch a new ping every WIFI_PING_INTERVAL_MS
				if (!g_ping_in_flight && (now - g_last_ping_ms >= WIFI_PING_INTERVAL_MS)) {
						IPAddress gw = WiFi.gatewayIP();
						if (gw != IPAddress(0, 0, 0, 0)) {
								wifi_ping_start(gw);
								g_last_ping_ms = now;
						}
				}
				return; // Connected — nothing more to do
		}

		// --- Outage in progress: check for reboot threshold ---
		const unsigned long total_outage = now - g_outage_start_ms;
		if (wifi_reconnect_should_reboot(total_outage, WIFI_REBOOT_AFTER_MS)) {
				LOGE("WiFi", "Total outage %lus exceeds threshold — rebooting", total_outage / 1000);
				ESP.restart();
				return; // unreachable
		}

		// --- Determine current tier ---
		const WifiReconnectTier tier = wifi_reconnect_get_tier(
				total_outage, WIFI_TIER1_DURATION_MS, WIFI_TIER2_DURATION_MS);

		// --- Tier 1: wait for SDK auto-reconnect ---
		if (g_wifi_state == WifiState::Tier1Wait) {
				if (tier >= WifiReconnectTier::Tier2) {
						g_wifi_state = WifiState::Tier2Retry;
						g_state_entry_ms = now;
						g_tier2_attempt = 0;
						g_last_retry_ms = 0;
						LOGW("WiFi", "Tier 1 expired (%lus) — escalating to Tier 2", total_outage / 1000);
				}
				return;
		}

		// --- Tier 2: active reconnect with exponential backoff ---
		if (g_wifi_state == WifiState::Tier2Retry) {
				if (tier >= WifiReconnectTier::Tier3) {
						g_wifi_state = WifiState::Tier3Reset;
						g_state_entry_ms = now;
						LOGW("WiFi", "Tier 2 expired (%lus) — escalating to Tier 3", total_outage / 1000);
						return;
				}

				const unsigned long backoff = wifi_reconnect_next_backoff(
						g_tier2_attempt, WIFI_TIER2_BACKOFF_BASE_MS, WIFI_TIER2_BACKOFF_MAX_MS);

				if (g_last_retry_ms == 0 || (now - g_last_retry_ms >= backoff)) {
						LOGI("WiFi", "Tier 2 retry #%u (backoff %lus)", g_tier2_attempt + 1, backoff / 1000);
						WiFi.disconnect(false);
						WiFi.begin(config->wifi_ssid, config->wifi_password);
						g_last_retry_ms = now;
						g_tier2_attempt++;
				}
				return;
		}

		// --- Tier 3: hard WiFi stack reset ---
		if (g_wifi_state == WifiState::Tier3Reset) {
				#ifdef CONFIG_IDF_TARGET_ESP32P4
				// P4-safe path: skip WIFI_OFF toggle (SDIO stability).
				LOGW("WiFi", "Tier 3: P4 safe reset (disconnect + begin)");
				WiFi.disconnect(false);
				WiFi.begin(config->wifi_ssid, config->wifi_password);
				g_wifi_state = WifiState::Tier3Reinit;
				g_state_entry_ms = now;
				#else
				// Non-P4: full WIFI_OFF cycle.
				const size_t int_free = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
				if (int_free < 80 * 1024) {
						LOGW("WiFi", "Tier 3: low internal RAM (%u bytes free) before reset", (unsigned)int_free);
				}
				LOGW("WiFi", "Tier 3: hard reset (WIFI_OFF cycle)");
				WiFi.disconnect(false);
				WiFi.mode(WIFI_OFF);
				g_wifi_state = WifiState::Tier3Off;
				g_state_entry_ms = now;
				#endif
				return;
		}

		// --- Tier 3 Off: waiting 1 s before re-init (non-P4 only) ---
		if (g_wifi_state == WifiState::Tier3Off) {
				if (now - g_state_entry_ms >= 1000) {
						LOGI("WiFi", "Tier 3: re-initializing WiFi stack");
						WiFi.mode(WIFI_STA);
						WiFi.begin(config->wifi_ssid, config->wifi_password);
						g_wifi_state = WifiState::Tier3Reinit;
						g_state_entry_ms = now;
				}
				return;
		}

		// --- Tier 3 Reinit: waiting for connection or 60 s retry ---
		if (g_wifi_state == WifiState::Tier3Reinit) {
				if (now - g_state_entry_ms >= WIFI_TIER3_REINIT_TIMEOUT_MS) {
						LOGW("WiFi", "Tier 3 reinit timeout — retrying hard reset");
						g_wifi_state = WifiState::Tier3Reset;
						g_state_entry_ms = now;
				}
				return;
		}
}
