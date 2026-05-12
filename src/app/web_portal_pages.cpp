#include "web_portal_pages.h"

#include "web_portal_auth.h"
#include "web_portal_state.h"

#include "web_assets.h"

#include <string.h>

static AsyncWebServerResponse *begin_gzipped_asset_response(
		AsyncWebServerRequest *request,
		const char *content_type,
		const uint8_t *content_gz,
		size_t content_gz_len,
		const char *cache_control
) {
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

// ---- Shell (new single-page root) ----

void handleShell(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		AsyncWebServerResponse *response = begin_gzipped_asset_response(
				request,
				"text/html",
				shell_html_gz,
				shell_html_gz_len,
				"no-store"
		);
		request->send(response);
}

// ---- Fragment handler ----

void handleFragment(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		// URL: /api/section/{id}  — id is the last path segment
		const String& url = request->url();
		int last_slash = url.lastIndexOf('/');
		if (last_slash < 0) {
				request->send(404, "text/plain", "Not found");
				return;
		}
		String frag_id = url.substring(last_slash + 1);

		const FragmentAsset* asset = find_fragment_asset(frag_id.c_str());
		if (!asset) {
				request->send(404, "text/plain", "Fragment not found");
				return;
		}

		AsyncWebServerResponse *response = begin_gzipped_asset_response(
				request,
				"text/html",
				asset->data,
				asset->len,
				"no-store"
		);
		request->send(response);
}

// ---- Legacy page handlers (redirect to shell with hash) ----

void handleRoot(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;

		if (web_portal_is_ap_mode_active()) {
				// In AP mode, serve shell which will show WiFi setup
				handleShell(request);
				return;
		}

		handleShell(request);
}

void handleHome(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;
		request->redirect("/#welcome");
}

void handlePad(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;
		request->redirect("/#pad-editor");
}

void handleNetwork(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;
		request->redirect("/#wifi");
}

void handleFirmware(AsyncWebServerRequest *request) {
		if (!portal_auth_gate(request)) return;
		request->redirect("/#ota-update");
}

// ---- CSS asset handlers ----

void handleBootstrapCSS(AsyncWebServerRequest *request) {
		AsyncWebServerResponse *response = begin_gzipped_asset_response(
				request,
				"text/css",
				bootstrap_min_css_gz,
				bootstrap_min_css_gz_len,
				"public, max-age=86400"
		);
		request->send(response);
}

void handlePortalCustomCSS(AsyncWebServerRequest *request) {
		AsyncWebServerResponse *response = begin_gzipped_asset_response(
				request,
				"text/css",
				portal_custom_css_gz,
				portal_custom_css_gz_len,
				"public, max-age=600"
		);
		request->send(response);
}

// ---- JS asset handler ----

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
