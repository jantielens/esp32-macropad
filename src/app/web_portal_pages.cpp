#include "web_portal_pages.h"

#include "web_portal_auth.h"
#include "web_portal_state.h"
#include "web_portal_utils.h"

#include "web_assets.h"

#include <string.h>

// Serve a gzipped PROGMEM asset as a length-aware chunked stream rather than a
// single beginResponse_P() blob. AsyncTCP only calls the filler when LWIP TX
// buffer space is available, so the response is naturally paced and
// DMA-internal SRAM pbufs recycle between chunks. This matters on ESP-Hosted
// SDIO boards (ESP32-P4 + ESP32-C6) where a parallel burst of large static
// asset responses (e.g. browser-issued /portal-all.css + /portal.js on a
// fresh portal load) can otherwise exhaust the DMA-internal pbuf pool and
// trigger a `copy_buff` NULL assert in `transport_drv_sta_tx`.
//
// HTTP_STREAM_CHUNK_SIZE (4 KB) matches the cap already used for file-backed
// streamed responses in web_portal_utils.cpp::sendFileThrottled().
static AsyncWebServerResponse *begin_gzipped_asset_response(
		AsyncWebServerRequest *request,
		const char *content_type,
		const uint8_t *content_gz,
		size_t content_gz_len,
		const char *cache_control
) {
		AsyncWebServerResponse *response = request->beginResponse(
				content_type,
				content_gz_len,
				[content_gz, content_gz_len](uint8_t *buffer, size_t max_len, size_t index) -> size_t {
						if (index >= content_gz_len) return 0;
						size_t remain = content_gz_len - index;
						size_t to_copy = (max_len < HTTP_STREAM_CHUNK_SIZE)
						                 ? max_len : HTTP_STREAM_CHUNK_SIZE;
						if (to_copy > remain) to_copy = remain;
						memcpy_P(buffer, content_gz + index, to_copy);
						return to_copy;
				}
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

		// In AP mode we deliberately do NOT 302-redirect to /#setup here:
		// captive-portal probes (Android's connectivitycheck.gstatic.com,
		// iOS's hotspot-detect.html) follow the redirect under the probe's
		// own hostname and end up looping. Serving the shell with HTTP 200
		// is what the captive-portal detector expects (non-204 = portal).
		// Routing to the setup wizard happens client-side via the nav API's
		// `primary.fragment` field, which portal_nav.js applies on init.
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

// ---- CSS asset handler ----
//
// Bootstrap + portal-custom are bundled into a single portal-all.css asset to
// reduce parallel HTTP requests at portal load. Fewer concurrent in-flight
// responses keeps DMA-internal SRAM fragmentation low on ESP-Hosted SDIO
// platforms (ESP32-P4 + ESP32-C6) where AsyncTCP TX buffers must come from
// MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL.
void handlePortalAllCSS(AsyncWebServerRequest *request) {
		AsyncWebServerResponse *response = begin_gzipped_asset_response(
				request,
				"text/css",
				portal_all_css_gz,
				portal_all_css_gz_len,
				"public, max-age=600"
		);
		request->send(response);
}

// ---- JS handler ----
//
// portal.js.bundle is split into named chunks (e.g. core, actions, config,
// pad, shell). The minifier enumerates the unique HAS_* flag set across
// chunks (today: HAS_DISPLAY) and emits one pre-gzipped PROGMEM blob per
// flag combination, wrapped in #if guards. Exactly one variant matches per
// build, so each board ships a single combined gzip stream and the browser
// fetches the entire portal JS in ONE request, with ONE gzip member.

void handlePortalJS(AsyncWebServerRequest *request) {
		AsyncWebServerResponse *response = begin_gzipped_asset_response(
				request,
				"application/javascript",
				portal_js_gz,
				portal_js_gz_len,
				"public, max-age=600"
		);
		request->send(response);
}

#if HAS_CAMERA
void handlePortalCameraJS(AsyncWebServerRequest *request) {
		AsyncWebServerResponse *response = begin_gzipped_asset_response(
				request,
				"application/javascript",
				portal_camera_js_gz,
				portal_camera_js_gz_len,
				"public, max-age=600"
		);
		request->send(response);
}

void handlePortalCameraCSS(AsyncWebServerRequest *request) {
		AsyncWebServerResponse *response = begin_gzipped_asset_response(
				request,
				"text/css",
				portal_camera_css_gz,
				portal_camera_css_gz_len,
				"public, max-age=600"
		);
		request->send(response);
}
#endif
