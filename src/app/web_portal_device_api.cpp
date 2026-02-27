#include "web_portal_device_api.h"

#include "web_portal_auth.h"
#include "web_portal_state.h"

#include "board_config.h"
#include "device_telemetry.h"
#include "repo_slug_config.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "project_branding.h"
#include "web_portal_json.h"
#if HEALTH_HISTORY_ENABLED
#include "health_history.h"
#endif
#include "../version.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#if HAS_DISPLAY
#include "display_manager.h"
#endif

// GET /api/mode - Return portal mode (core vs full)
void handleGetMode(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		AsyncResponseStream *response = request->beginResponseStream("application/json");
		response->print("{\"mode\":\"");
		response->print(web_portal_is_ap_mode_active() ? "core" : "full");
		response->print("\",\"ap_active\":");
		response->print(web_portal_is_ap_mode_active() ? "true" : "false");
		response->print("}");
		request->send(response);
}

// GET /api/info - Get device information
void handleGetVersion(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		AsyncResponseStream *response = request->beginResponseStream("application/json");
		response->print("{\"version\":\"");
		response->print(FIRMWARE_VERSION);
		response->print("\",\"build_date\":\"");
		response->print(BUILD_DATE);
		response->print("\",\"build_time\":\"");
		response->print(BUILD_TIME);
		response->print("\",\"chip_model\":\"");
		response->print(ESP.getChipModel());
		response->print("\",\"chip_revision\":");
		response->print(ESP.getChipRevision());
		response->print(",\"chip_cores\":");
		response->print(ESP.getChipCores());
		response->print(",\"cpu_freq\":");
		response->print(ESP.getCpuFreqMHz());
		response->print(",\"flash_chip_size\":");
		response->print(ESP.getFlashChipSize());
		response->print(",\"psram_size\":");
		response->print(ESP.getPsramSize());
		response->print(",\"free_heap\":");
		response->print(ESP.getFreeHeap());
		response->print(",\"sketch_size\":");
		response->print(device_telemetry_sketch_size());
		response->print(",\"free_sketch_space\":");
		response->print(device_telemetry_free_sketch_space());
		response->print(",\"mac_address\":\"");
		response->print(WiFi.macAddress());
		response->print("\",\"wifi_hostname\":\"");
		response->print(WiFi.getHostname());
		response->print("\",\"mdns_name\":\"");
		response->print(WiFi.getHostname());
		response->print(".local\",\"hostname\":\"");
		response->print(WiFi.getHostname());
		response->print("\",\"project_name\":\"");
		response->print(PROJECT_NAME);
		response->print("\",\"project_display_name\":\"");
		response->print(PROJECT_DISPLAY_NAME);

		// Build metadata for GitHub Pages-based updates
		response->print("\",\"board_name\":\"");
		#ifdef BUILD_BOARD_NAME
				response->print(BUILD_BOARD_NAME);
		#else
				response->print("unknown");
		#endif

		// Close board_name string value
		response->print("\"");

		// Health widget client tuning (sparklines + polling cadence)
		response->print(",\"health_poll_interval_ms\":");
		response->print((unsigned long)HEALTH_POLL_INTERVAL_MS);
		response->print(",\"health_history_seconds\":");
		response->print((unsigned long)HEALTH_HISTORY_SECONDS);

		// Optional: Device-side history endpoint (/api/health/history)
		#if HEALTH_HISTORY_ENABLED
				const bool hist_available = health_history_available();
				const HealthHistoryParams hist_params = health_history_params();
				response->print(",\"health_history_available\":");
				response->print(hist_available ? "true" : "false");
				response->print(",\"health_history_period_ms\":");
				response->print((unsigned long)hist_params.period_ms);
				response->print(",\"health_history_samples\":");
				response->print((unsigned long)hist_params.samples);
		#else
				response->print(",\"health_history_available\":false");
				response->print(",\"health_history_period_ms\":0");
				response->print(",\"health_history_samples\":0");
		#endif

		response->print(",\"github_owner\":\"");
		response->print(REPO_OWNER);
		response->print("\",\"github_repo\":\"");
		response->print(REPO_NAME);
		response->print("\"");

		response->print(",\"has_mqtt\":");
		response->print(HAS_MQTT ? "true" : "false");
		response->print(",\"has_ble\":");
		response->print(HAS_BLE ? "true" : "false");
		response->print(",\"has_backlight\":");
		response->print(HAS_BACKLIGHT ? "true" : "false");

		#if HAS_DISPLAY
				// Display screen information
				response->print(",\"has_display\":true");

				// Display resolution (driver coordinate space for direct writes / image upload)
				int display_coord_width = DISPLAY_WIDTH;
				int display_coord_height = DISPLAY_HEIGHT;
				if (displayManager && displayManager->getDriver()) {
						display_coord_width = displayManager->getDriver()->width();
						display_coord_height = displayManager->getDriver()->height();
				}
				response->print(",\"display_coord_width\":");
				response->print(display_coord_width);
				response->print(",\"display_coord_height\":");
				response->print(display_coord_height);

				// Get available screens
				size_t screen_count = 0;
				const ScreenInfo* screens = display_manager_get_available_screens(&screen_count);

				response->print(",\"available_screens\":[");
				for (size_t i = 0; i < screen_count; i++) {
						if (i > 0) response->print(",");
						response->print("{\"id\":\"");
						response->print(screens[i].id);
						response->print("\",\"name\":\"");
						response->print(screens[i].display_name);
						response->print("\"}");
				}
				response->print("]");

				// Get current screen
				const char* current_screen = display_manager_get_current_screen_id();
				response->print(",\"current_screen\":");
				if (current_screen) {
						response->print("\"");
						response->print(current_screen);
						response->print("\"");
				} else {
						response->print("null");
				}
		#else
				response->print(",\"has_display\":false");
		#endif

		response->print("}");
		request->send(response);
}

// GET /api/health - Get device health statistics
void handleGetHealth(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		std::shared_ptr<BasicJsonDocument<PsramJsonAllocator>> doc = make_psram_json_doc(2048);
		if (doc && doc->capacity() > 0) {
				device_telemetry_fill_api(*doc);
				if (doc->overflowed()) {
						LOGE("Portal", "/api/health JSON overflow");
				}
		}

		web_portal_send_json_chunked(request, doc);
}

// GET /api/health/history - Get device-side health history for sparklines
void handleGetHealthHistory(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		#if !HEALTH_HISTORY_ENABLED
				request->send(404, "application/json", "{\"available\":false}");
		#else

		if (!health_history_available()) {
				request->send(404, "application/json", "{\"available\":false}");
				return;
		}

		const HealthHistoryParams params = health_history_params();
		const size_t count = health_history_count();
		const size_t capacity = health_history_capacity();

		AsyncResponseStream *response = request->beginResponseStream("application/json");
		response->addHeader("Cache-Control", "no-store");

		response->print("{\"available\":true");
		response->print(",\"period_ms\":");
		response->print((unsigned long)params.period_ms);
		response->print(",\"seconds\":");
		response->print((unsigned long)params.seconds);
		response->print(",\"samples\":");
		response->print((unsigned long)params.samples);
		response->print(",\"count\":");
		response->print((unsigned long)count);
		response->print(",\"capacity\":");
		response->print((unsigned long)capacity);

		// Helper macro to print a uint32 array field with minimal overhead.
#define PRINT_U32_ARRAY_FIELD(field_name, expr_u32) \
		do { \
				response->print(",\""); \
				response->print(field_name); \
				response->print("\":["); \
				for (size_t i = 0; i < count; i++) { \
						if (i > 0) response->print(","); \
						HealthHistorySample s = {}; \
						if (health_history_get_sample(i, &s)) { \
								response->print((unsigned long)(expr_u32)); \
						} else { \
								response->print("0"); \
						} \
				} \
				response->print("]"); \
		} while (0)

		// cpu_usage is int16 and may be -1 (unknown).
		response->print(",\"cpu_usage\":[");
		for (size_t i = 0; i < count; i++) {
				if (i > 0) response->print(",");
				HealthHistorySample s = {};
				if (health_history_get_sample(i, &s)) {
						if (s.cpu_usage < 0) {
								response->print("null");
						} else {
								response->print((int)s.cpu_usage);
						}
				} else {
						response->print("null");
				}
		}
		response->print("]");

		PRINT_U32_ARRAY_FIELD("uptime_ms", s.uptime_ms);

		PRINT_U32_ARRAY_FIELD("heap_internal_free", s.heap_internal_free);
		PRINT_U32_ARRAY_FIELD("heap_internal_free_min_window", s.heap_internal_free_min_window);
		PRINT_U32_ARRAY_FIELD("heap_internal_free_max_window", s.heap_internal_free_max_window);

		PRINT_U32_ARRAY_FIELD("psram_free", s.psram_free);
		PRINT_U32_ARRAY_FIELD("psram_free_min_window", s.psram_free_min_window);
		PRINT_U32_ARRAY_FIELD("psram_free_max_window", s.psram_free_max_window);

		PRINT_U32_ARRAY_FIELD("heap_internal_largest", s.heap_internal_largest);
		PRINT_U32_ARRAY_FIELD("heap_internal_largest_min_window", s.heap_internal_largest_min_window);
		PRINT_U32_ARRAY_FIELD("heap_internal_largest_max_window", s.heap_internal_largest_max_window);

#undef PRINT_U32_ARRAY_FIELD

		response->print("}");
		request->send(response);

		#endif // HEALTH_HISTORY_ENABLED
}

// POST /api/reboot - Reboot device without saving
void handleReboot(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		LOGI("API", "POST /api/reboot");
		request->send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting device...\"}");

		// Schedule reboot after response is sent
		delay(100);
		LOGI("Portal", "Rebooting");
		ESP.restart();
}
