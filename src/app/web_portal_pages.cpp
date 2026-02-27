#include "web_portal_pages.h"

#include "web_portal_auth.h"
#include "web_portal_state.h"

#include "web_assets.h"

static AsyncWebServerResponse *begin_gzipped_asset_response(
		AsyncWebServerRequest *request,
		const char *content_type,
		const uint8_t *content_gz,
		size_t content_gz_len,
		const char *cache_control
) {
		// Prefer the PROGMEM-aware response helper to avoid accidental heap copies.
		// All generated assets live in flash as `const uint8_t[] PROGMEM`.
		AsyncWebServerResponse *response = request->beginResponse_P(
				200,
				content_type,
				content_gz,
				content_gz_len
		);

		response->addHeader("Content-Encoding", "gzip");
		response->addHeader("Vary", "Accept-Encoding");
		if (cache_control && strlen(cache_control) > 0) {
				response->addHeader("Cache-Control", cache_control);
		}
		return response;
}

void handleRoot(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		if (web_portal_is_ap_mode_active()) {
				// In AP mode, redirect to network configuration page
				request->redirect("/network.html");
				return;
		}

		// In full mode, serve home page
		AsyncWebServerResponse *response = begin_gzipped_asset_response(
				request,
				"text/html",
				home_html_gz,
				home_html_gz_len,
				"no-store"
		);
		request->send(response);
}

void handleHome(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		if (web_portal_is_ap_mode_active()) {
				// In AP mode, redirect to network configuration page
				request->redirect("/network.html");
				return;
		}

		AsyncWebServerResponse *response = begin_gzipped_asset_response(
				request,
				"text/html",
				home_html_gz,
				home_html_gz_len,
				"no-store"
		);
		request->send(response);
}

void handleNetwork(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		AsyncWebServerResponse *response = begin_gzipped_asset_response(
				request,
				"text/html",
				network_html_gz,
				network_html_gz_len,
				"no-store"
		);
		request->send(response);
}

void handleFirmware(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		if (web_portal_is_ap_mode_active()) {
				// In AP mode, redirect to network configuration page
				request->redirect("/network.html");
				return;
		}

		AsyncWebServerResponse *response = begin_gzipped_asset_response(
				request,
				"text/html",
				firmware_html_gz,
				firmware_html_gz_len,
				"no-store"
		);
		request->send(response);
}

void handleCSS(AsyncWebServerRequest *request) {
		AsyncWebServerResponse *response = begin_gzipped_asset_response(
				request,
				"text/css",
				portal_css_gz,
				portal_css_gz_len,
				"public, max-age=600"
		);
		request->send(response);
}

void handleJS(AsyncWebServerRequest *request) {
		AsyncWebServerResponse *response = begin_gzipped_asset_response(
				request,
				"application/javascript",
				portal_js_gz,
				portal_js_gz_len,
				"public, max-age=600"
		);
		request->send(response);
}
