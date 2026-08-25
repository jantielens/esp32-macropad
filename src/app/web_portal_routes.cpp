#include "web_portal_routes.h"
#include "web_portal_auth.h"
#include "web_portal_component_api.h"
#include "web_portal_cors.h"
#include "web_portal_config.h"
#include "web_portal_device_api.h"
#include "web_portal_firmware.h"
#include "web_portal_icons.h"
#include "web_portal_ota.h"
#include "web_portal_pad.h"
#include "web_portal_pages.h"

#include "board_config.h"

#if HAS_DISPLAY
#include "web_portal_screenshot.h"
#endif

#if HAS_BLE_HID
#include "web_portal_ble.h"
#endif

#if HAS_SOUND_PLAYER
#include "web_portal_sounds.h"
#endif

#if HAS_CAMERA
#include "web_portal_camera.h"
#endif

// ============================================================================
// Route initializer registry — populated by REGISTER_ROUTES() in feature .cpp
// files aggregated through route_components.cpp at the sketch root.
// ============================================================================

static constexpr uint8_t ROUTE_INIT_REGISTRY_MAX = 16;
static RouteInitFn s_route_initializers[ROUTE_INIT_REGISTRY_MAX] = {};
static uint8_t     s_route_initializer_count = 0;

bool web_portal_routes_add(RouteInitFn fn) {
    if (!fn) return false;
    if (s_route_initializer_count >= ROUTE_INIT_REGISTRY_MAX) return false;
    s_route_initializers[s_route_initializer_count++] = fn;
    return true;
}

void web_portal_routes_register_all(AsyncWebServer* server) {
    for (uint8_t i = 0; i < s_route_initializer_count; ++i) {
        s_route_initializers[i](server);
    }
}

void web_portal_register_routes(AsyncWebServer* server) {
		auto handleCorsPreflight = [](AsyncWebServerRequest *request) {
				web_portal_send_cors_preflight(request);
		};

		auto registerOptions = [server, handleCorsPreflight](const char* path) {
				server->on(path, HTTP_OPTIONS, handleCorsPreflight);
		};

		// Page routes
		server->on("/", HTTP_GET, handleRoot);
		server->on("/home.html", HTTP_GET, handleHome);
		server->on("/pads.html", HTTP_GET, handlePad);
		server->on("/network.html", HTTP_GET, handleNetwork);
		server->on("/firmware.html", HTTP_GET, handleFirmware);

		// Asset routes
		// /portal.js returns a single pre-gzipped PROGMEM blob whose contents are
		// selected at compile time from the chunked portal.js.bundle manifest.
		// Exactly one #if variant matches per build, so the browser fetches the
		// entire portal JS in one request with one gzip member (see handlePortalJS).
		server->on("/portal.js", HTTP_GET, handlePortalJS);
		#if HAS_CAMERA
		server->on("/portal-camera.js", HTTP_GET, handlePortalCameraJS);
		server->on("/portal-camera.css", HTTP_GET, handlePortalCameraCSS);
		#endif
		server->on("/portal-all.css", HTTP_GET, handlePortalAllCSS);

		// API endpoints
		// NOTE: Keep more specific routes registered before more general/prefix routes.
		// Some AsyncWebServer matchers can behave like prefix matches depending on configuration.
		registerOptions("/api/config");
		server->on("/api/config", HTTP_GET, handleGetConfig);

		server->on(
				"/api/config",
				HTTP_POST,
				[](AsyncWebServerRequest *request) {
						if (!portal_auth_gate(request)) return;
				},
				NULL,
				handlePostConfig
		);

		server->on("/api/config", HTTP_DELETE, handleDeleteConfig);

		registerOptions("/api/info");
		server->on("/api/info", HTTP_GET, handleGetVersion);
		#if HEALTH_HISTORY_ENABLED
		server->on("/api/health/history", HTTP_GET, handleGetHealthHistory);
		#endif
		#if HEALTH_HISTORY_ENABLED
		registerOptions("/api/health/history");
		#endif
		registerOptions("/api/health");
		server->on("/api/health", HTTP_GET, handleGetHealth);

		registerOptions("/api/reboot");
		server->on("/api/reboot", HTTP_POST, handleReboot);

		// GitHub Pages-based firmware updates (URL-driven)
		registerOptions("/api/firmware/update/status");
		server->on("/api/firmware/update/status", HTTP_GET, handleGetFirmwareUpdateStatus);
		server->on(
				"/api/firmware/update",
				HTTP_POST,
				[](AsyncWebServerRequest *request) {
						if (!portal_auth_gate(request)) return;
				},
				NULL,
				handlePostFirmwareUpdate
		);
		registerOptions("/api/firmware/update");

#if HAS_DISPLAY
		// Pad button sizes (registered before /api/pad to avoid prefix match)
		registerOptions("/api/pad/button_sizes");
		server->on("/api/pad/button_sizes", HTTP_GET, handleGetButtonSizes);

		// Pad building blocks catalog (registered before /api/pad to avoid prefix match)
		registerOptions("/api/pad/blocks");
		server->on("/api/pad/blocks", HTTP_GET, handleGetPadBlocks);

#if HAS_MQTT
		// Pad binding live-resolve (registered before /api/pad to avoid prefix match)
		registerOptions("/api/pad/resolve");
		server->on(
				"/api/pad/resolve",
				HTTP_POST,
				[](AsyncWebServerRequest *request) {},
				NULL,
				handlePostPadResolve
		);
#endif

		// Pad config API
		registerOptions("/api/pad");
		server->on("/api/pad", HTTP_GET, handleGetPadConfig);
		server->on(
				"/api/pad",
				HTTP_POST,
				[](AsyncWebServerRequest *request) {
						if (!portal_auth_gate(request)) return;
				},
				NULL,
				handlePostPadConfig
		);
		server->on("/api/pad", HTTP_DELETE, handleDeletePadConfig);



		registerOptions("/api/screenshot");
		server->on("/api/screenshot", HTTP_GET, handleGetScreenshot);
		#if HAS_TOUCH
		registerOptions("/api/screen/tap");
		server->on("/api/screen/tap", HTTP_POST, handlePostScreenTap);
		#endif

		registerOptions("/api/icons/install");
		server->on(
				"/api/icons/install",
				HTTP_POST,
				[](AsyncWebServerRequest *request) {
						if (!portal_auth_gate(request)) return;
				},
				NULL,
				handlePostIconInstall
		);

		registerOptions("/api/icons/page");
		server->on("/api/icons/page", HTTP_DELETE, handleDeletePageIcons);

		registerOptions("/api/icons/installed");
		server->on("/api/icons/installed", HTTP_GET, handleGetInstalledIcons);

		// Icon debug endpoints
		registerOptions("/api/icons/files");
		server->on("/api/icons/files", HTTP_GET, handleGetIconFiles);

		registerOptions("/api/icons/cache");
		server->on("/api/icons/cache", HTTP_GET, handleGetIconCache);

		registerOptions("/api/icons/file");
		server->on("/api/icons/file", HTTP_GET, handleGetIconFile);
		server->on("/api/icons/file", HTTP_DELETE, handleDeleteIconFile);

#endif

#if HAS_CAMERA
		registerOptions("/api/camera/snapshot.raw");
		server->on("/api/camera/snapshot.raw", HTTP_GET, handleGetCameraRawSnapshot);

		registerOptions("/api/camera/snapshot.jpg");
		server->on("/api/camera/snapshot.jpg", HTTP_GET, handleGetCameraJpegSnapshot);

		server->on("/api/camera/stream", HTTP_GET, handleGetCameraMjpegStream);
#endif

		// OTA upload endpoint
		server->on(
				"/api/update",
				HTTP_POST,
				[](AsyncWebServerRequest *request) {
						if (!portal_auth_gate(request)) return;
				},
				handleOTAUpload
		);
		registerOptions("/api/update");

#if HAS_BLE_HID
		// BLE pairing endpoint
		registerOptions("/api/ble/pairing/start");
		server->on("/api/ble/pairing/start", HTTP_POST, [](AsyncWebServerRequest *request) {
				if (!portal_auth_gate(request)) return;
				handlePostBlePairingStart(request);
		});
#endif

#if HAS_SOUND_PLAYER
		// Sound file management endpoints
		registerOptions("/api/sounds/upload");
		server->on(
				"/api/sounds/upload",
				HTTP_POST,
				[](AsyncWebServerRequest *request) {
						if (!portal_auth_gate(request)) return;
				},
				NULL,
				handlePostSoundUpload
		);

		registerOptions("/api/sounds/list");
		server->on("/api/sounds/list", HTTP_GET, handleGetSoundList);

		registerOptions("/api/sounds");
		server->on("/api/sounds", HTTP_DELETE, handleDeleteSound);

		registerOptions("/api/sounds/play");
		server->on("/api/sounds/play", HTTP_POST, handlePostSoundPlay);
#endif

		// Feature- and device-class route initializers self-registered via
		// REGISTER_ROUTES(). Run them before the generic component API so any
		// exact-match routes they install take precedence over the wildcard
		// component dispatcher.
		web_portal_routes_register_all(server);

		// Fragment serving — /api/section/{id}
		// Uses wildcard so /api/section/wifi, /api/section/pad-editor, etc. all match.
		// Registered before generic component routes to avoid prefix-match conflicts.
		server->on("/api/section/*", HTTP_GET, handleFragment);

		// Generic component API routes — MUST be registered LAST (first-match routing).
		// Dispatches to any component registered via REGISTER_COMPONENT().
		web_portal_register_component_routes(server);

}
