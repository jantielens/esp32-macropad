#include "web_portal_auth.h"

#include "web_portal_state.h"
#include "portal_idle.h"
#include "config_manager.h"
#include "class_branding.h"
#include "net_activity.h"
#if HAS_AUTO_SHUTDOWN
#include "auto_shutdown.h"

static void portal_user_activity(AsyncWebServerRequest* request) {
		if (request->method() == HTTP_POST || request->method() == HTTP_PUT ||
				request->method() == HTTP_PATCH || request->method() == HTTP_DELETE)
				auto_shutdown_notify_activity();
}
#endif

static bool portal_auth_required() {
		if (web_portal_is_recovery_ap_active()) return false;

		DeviceConfig *config = web_portal_get_current_config();
		if (!config) return false;

		return config->basic_auth_enabled;
}

bool portal_auth_gate(AsyncWebServerRequest *request) {
		net_activity_mark(NET_CH_PORTAL);
		portal_idle_notify_activity();

		if (!portal_auth_required()) {
#if HAS_AUTO_SHUTDOWN
				portal_user_activity(request);
#endif
				return true;
		}

		DeviceConfig *config = web_portal_get_current_config();
		if (!config) return true;

		const char *user = config->basic_auth_username;
		const char *pass = config->basic_auth_password;

		if (request->authenticate(user, pass)) {
#if HAS_AUTO_SHUTDOWN
				portal_user_activity(request);
#endif
				return true;
		}

		request->requestAuthentication(device_class_get_full_name());
		return false;
}
