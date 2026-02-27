#include "web_portal_auth.h"

#include "web_portal_state.h"
#include "portal_idle.h"
#include "config_manager.h"
#include "project_branding.h"

static bool portal_auth_required() {
		if (web_portal_is_ap_mode_active()) return false;

		DeviceConfig *config = web_portal_get_current_config();
		if (!config) return false;

		return config->basic_auth_enabled;
}

bool portal_auth_gate(AsyncWebServerRequest *request) {
		portal_idle_notify_activity();

		if (!portal_auth_required()) return true;

		DeviceConfig *config = web_portal_get_current_config();
		if (!config) return true;

		const char *user = config->basic_auth_username;
		const char *pass = config->basic_auth_password;

		if (request->authenticate(user, pass)) {
				return true;
		}

		request->requestAuthentication(PROJECT_DISPLAY_NAME);
		return false;
}
