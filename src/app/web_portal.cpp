/*
 * Web Configuration Portal Implementation
 * 
 * Async web server with captive portal support.
 * Serves static files and provides REST API for configuration.
 */

// AsyncTCP task stack sizing:
// - The AsyncTCP library is compiled as a separate translation unit.
// - Defining CONFIG_ASYNC_TCP_STACK_SIZE in this file does NOT reliably affect the library build.
// - To override it, define CONFIG_ASYNC_TCP_STACK_SIZE in src/boards/<board>/board_overrides.h.
//   The build script propagates this allowlisted define into library builds.

#include "web_portal.h"
#include "config_manager.h"
#include "log_manager.h"
#include "board_config.h"
#include "device_telemetry.h"
#include "project_branding.h"
#include "../version.h"
#include "psram_json_allocator.h"
#include "web_portal_routes.h"
#include "web_portal_auth.h"
#include "web_portal_config.h"
#include "web_portal_cors.h"
#include "web_portal_state.h"
#include "portal_idle.h"
#include "web_portal_firmware.h"
#include "web_portal_ap.h"

#if HAS_DISPLAY
#include "display_manager.h"
#include "screen_saver_manager.h"
#endif

#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include <esp_heap_caps.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static void log_async_tcp_stack_watermark_once() {
		static bool logged = false;
		if (logged) return;
		logged = true;

		// AsyncTCP task name varies by library/core version; try a few common ones.
		TaskHandle_t task = xTaskGetHandle("async_tcp");
		if (!task) task = xTaskGetHandle("async_tcp_task");
		if (!task) task = xTaskGetHandle("AsyncTCP");
		if (!task) return;

		const UBaseType_t high_water_words = uxTaskGetStackHighWaterMark(task);
		const unsigned high_water_bytes = (unsigned)high_water_words * (unsigned)sizeof(StackType_t);

		#ifdef CONFIG_ASYNC_TCP_STACK_SIZE
				LOGI("Portal", "AsyncTCP stack watermark: %u bytes (CONFIG_ASYNC_TCP_STACK_SIZE=%u)", high_water_bytes, (unsigned)CONFIG_ASYNC_TCP_STACK_SIZE);
		#else
				LOGI("Portal", "AsyncTCP stack watermark: %u bytes (CONFIG_ASYNC_TCP_STACK_SIZE not set)", high_water_bytes);
		#endif
}


// Web server on port 80 (pointer to avoid constructor issues)
AsyncWebServer *server = nullptr;

// State
static DeviceConfig *current_config = nullptr;
static bool ota_in_progress = false;

bool web_portal_is_ap_mode_active() {
		return web_portal_is_ap_mode();
}

DeviceConfig* web_portal_get_current_config() {
		return current_config;
}

void web_portal_set_ota_in_progress(bool in_progress) {
		ota_in_progress = in_progress;
}

// ===== Basic Auth (optional; STA/full mode only) =====
// (Basic auth gate moved to web_portal_auth.cpp)

// ===== PUBLIC API =====

// Initialize web portal
void web_portal_init(DeviceConfig *config) {
		LOGI("Portal", "Init start");
		
		current_config = config;
		LOGI("Portal", "Config ptr: %p, backlight_brightness: %d", 
										current_config, current_config->backlight_brightness);
		
		// Create web server instance (avoid global constructor issues)
		if (server == nullptr) {
				yield();
				delay(100);
				
				server = new AsyncWebServer(80);
				
				yield();
				delay(100);
		}

		// CORS default headers for GitHub Pages (if repo slug is available).
		web_portal_add_default_cors_headers();

		// Routes (factored out for maintainability)
		web_portal_register_routes(server);
		
		// Captive portal 404 handler
		web_portal_ap_register_not_found(server);
		
		// Start server
		yield();
		delay(100);
		server->begin();

		log_async_tcp_stack_watermark_once();
		LOGI("Portal", "Init complete");
}

// Handle web server (call in loop)
void web_portal_handle() {
		web_portal_ap_handle();

		web_portal_config_loop();

		portal_idle_loop();
}

// Check if OTA update is in progress
bool web_portal_ota_in_progress() {
		return ota_in_progress;
}
