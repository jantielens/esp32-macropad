#include "web_portal_cors.h"

#include "repo_slug_config.h"

#include <Arduino.h>

static String g_cors_origin;

// CORS is restricted to the GitHub Pages origin.
static constexpr bool kAllowAllOrigins = false;

static void ensure_urls() {
		if (g_cors_origin.length() > 0) {
				return;
		}

		if (strlen(REPO_OWNER) == 0 || strlen(REPO_NAME) == 0) {
				return;
		}

		g_cors_origin = String("https://") + REPO_OWNER + ".github.io";
}

const char* web_portal_cors_origin() {
		ensure_urls();
		if (kAllowAllOrigins) return "*";
		return g_cors_origin.c_str();
}

static constexpr const char kCorsAllowHeaders[] = "Authorization, Content-Type";
static constexpr const char kCorsAllowMethods[] = "GET,POST,PUT,DELETE,OPTIONS";

void web_portal_add_default_cors_headers() {
		const char* origin = web_portal_cors_origin();
		if (!origin || strlen(origin) == 0) return;

		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", origin);
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", kCorsAllowHeaders);
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", kCorsAllowMethods);
		DefaultHeaders::Instance().addHeader("Vary", "Origin");
}

void web_portal_add_cors_headers(AsyncWebServerResponse* response) {
		if (!response) return;

		const char* origin = web_portal_cors_origin();
		if (!origin || strlen(origin) == 0) return;

		response->addHeader("Access-Control-Allow-Origin", origin);
		response->addHeader("Access-Control-Allow-Headers", kCorsAllowHeaders);
		response->addHeader("Access-Control-Allow-Methods", kCorsAllowMethods);
		response->addHeader("Vary", "Origin");
}

void web_portal_send_cors_preflight(AsyncWebServerRequest* request) {
		if (!request) return;
		AsyncWebServerResponse* response = request->beginResponse(204);
		web_portal_add_cors_headers(response);
		request->send(response);
}
