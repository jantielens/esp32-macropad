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

		// Allow 0 (blank) — the MIN_USER_BRIGHTNESS floor is enforced on the
		// persisted config path (web_portal_config.cpp), not on this transient
		// runtime endpoint which the browser uses for blank-on-save sequences.
		if (brightness < 0) brightness = 0;
		if (brightness > 100) brightness = 100;

		LOGI("API", "PUT /api/display/brightness: %d%%", brightness);

		// Route through the screen saver manager so internal tracking, config,
		// and awake/asleep transitions are handled consistently.
		screen_saver_manager_set_brightness(brightness);

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
