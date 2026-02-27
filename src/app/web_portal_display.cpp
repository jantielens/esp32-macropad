#include "web_portal_display.h"

#if HAS_DISPLAY

#include "web_portal_auth.h"
#include "web_portal_state.h"

#include "log_manager.h"
#include "board_config.h"

#include "display_manager.h"
#include "screen_saver_manager.h"

#include <ArduinoJson.h>

void handleSetDisplayBrightness(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
		if (!portal_auth_gate(request)) return;

		// Only handle the complete request (index == 0 && index + len == total)
		if (index != 0 || index + len != total) {
				return;
		}

		StaticJsonDocument<128> doc;
		DeserializationError error = deserializeJson(doc, data, len);

		if (error) {
				request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
				return;
		}

		if (!doc.containsKey("brightness")) {
				request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing brightness\"}");
				return;
		}

		int brightness = doc["brightness"];

		if (brightness < 0) brightness = 0;
		if (brightness > 100) brightness = 100;

		LOGI("API", "PUT /api/display/brightness: %d%%", brightness);

		// Update the in-RAM target brightness (does not persist to NVS).
		// This keeps the screen saver target consistent with what the user sees.
		DeviceConfig *config = web_portal_get_current_config();
		if (config) {
				config->backlight_brightness = brightness;
		}

		// Edge case: if the screen saver is dimming/asleep/fading, directly setting the
		// backlight would show the UI again without updating the screen saver state.
		// Easiest fix: when not Awake, route through the screen saver wake path.
		const ScreenSaverState state = screen_saver_manager_get_status().state;
		if (state != ScreenSaverState::Awake) {
				screen_saver_manager_wake();
		} else {
				display_manager_set_backlight_brightness(brightness);
				screen_saver_manager_notify_activity(false);
		}

		char response[64];
		snprintf(response, sizeof(response), "{\"success\":true,\"brightness\":%d}", brightness);
		request->send(200, "application/json", response);
}

void handleGetDisplaySleep(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		ScreenSaverStatus status = screen_saver_manager_get_status();

		StaticJsonDocument<256> doc;
		doc["enabled"] = status.enabled;
		doc["state"] = (uint8_t)status.state;
		doc["current_brightness"] = status.current_brightness;
		doc["target_brightness"] = status.target_brightness;
		doc["seconds_until_sleep"] = status.seconds_until_sleep;

		AsyncResponseStream *response = request->beginResponseStream("application/json");
		serializeJson(doc, *response);
		request->send(response);
}

void handlePostDisplaySleep(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		LOGI("API", "POST /api/display/sleep");
		screen_saver_manager_sleep_now();
		request->send(200, "application/json", "{\"success\":true}");
}

void handlePostDisplayWake(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		LOGI("API", "POST /api/display/wake");
		screen_saver_manager_wake();
		request->send(200, "application/json", "{\"success\":true}");
}

void handlePostDisplayActivity(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		bool wake = false;
		if (request->hasParam("wake")) {
				wake = (request->getParam("wake")->value() == "1");
		}

		LOGI("API", "POST /api/display/activity (wake=%d)", (int)wake);
		screen_saver_manager_notify_activity(wake);
		request->send(200, "application/json", "{\"success\":true}");
}

void handleSetDisplayScreen(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
		if (!portal_auth_gate(request)) return;

		// Only handle the complete request (index == 0 && index + len == total)
		if (index != 0 || index + len != total) {
				return;
		}

		StaticJsonDocument<256> doc;
		DeserializationError error = deserializeJson(doc, data, len);

		if (error) {
				request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
				return;
		}

		if (!doc.containsKey("screen")) {
				request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing screen ID\"}");
				return;
		}

		const char *screen_id = doc["screen"];
		if (!screen_id || strlen(screen_id) == 0) {
				request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid screen ID\"}");
				return;
		}

		LOGI("API", "PUT /api/display/screen: %s", screen_id);

		bool success = false;
		display_manager_show_screen(screen_id, &success);

		// Screen-affecting action counts as explicit activity and should wake.
		if (success) {
				screen_saver_manager_notify_activity(true);
		}

		if (success) {
				char response[96];
				snprintf(response, sizeof(response), "{\"success\":true,\"screen\":\"%s\"}", screen_id);
				request->send(200, "application/json", response);
		} else {
				request->send(404, "application/json", "{\"success\":false,\"message\":\"Screen not found\"}");
		}
}

#endif // HAS_DISPLAY
