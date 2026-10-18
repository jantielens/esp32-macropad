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
#include "web_portal_recipes.h"
#include "web_portal_logs.h"
#if HAS_BOOT_LOGO
#include "web_portal_boot_logo.h"
#endif

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
#if HAS_TTF_FONTS
#include "web_portal_fonts.h"
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

static const char* const core_preflight_paths[] = {
	"/api/config",
	"/api/info",
	"/api/bindings",
#if HEALTH_HISTORY_ENABLED
	"/api/health/history",
#endif
	"/api/health",
	"/api/reboot",
	"/api/firmware/update/status",
	"/api/firmware/update",
#if HAS_DISPLAY
	"/api/recipes/catalog",
	"/api/pad/button_sizes",
	"/api/pad/blocks",
#if HAS_MQTT
	"/api/pad/resolve",
#endif
	"/api/pad",
	"/api/screenshot",
#if HAS_TOUCH
	"/api/screen/tap",
#endif
	"/api/icons/install",
	"/api/icons/page",
	"/api/icons/installed",
	"/api/icons/files",
	"/api/icons/cache",
	"/api/icons/file",
#endif
#if HAS_CAMERA
	"/api/camera/snapshot.raw",
	"/api/camera/snapshot.jpg",
#endif
	"/api/update",
#if HAS_BLE_HID
	"/api/ble/pairing/start",
#endif
#if HAS_TTF_FONTS
	"/api/fonts/list",
	"/api/fonts/upload",
	"/api/fonts",
#endif
#if HAS_SOUND_PLAYER
	"/api/sounds/upload",
	"/api/sounds/list",
	"/api/sounds",
	"/api/sounds/play",
#endif
#if HAS_BOOT_LOGO
	"/api/boot-logo",
	"/api/boot-logo/thumbnail",
	"/api/boot-logo/upload",
#endif
};

class CorePreflightHandler : public AsyncWebHandler {
public:
	bool canHandle(AsyncWebServerRequest* request) const override {
		if (!request || request->method() != HTTP_OPTIONS) return false;
		const String& url = request->url();
		for (const char* path : core_preflight_paths) {
			const size_t length = strlen(path);
			if (strncmp(url.c_str(), path, length) == 0 &&
				(url.c_str()[length] == '\0' || url.c_str()[length] == '/')) {
				return true;
			}
		}
		return false;
	}

	void handleRequest(AsyncWebServerRequest* request) override {
		web_portal_send_cors_preflight(request);
	}
};

void web_portal_register_routes(AsyncWebServer* server) {
		server->addHandler(new CorePreflightHandler());

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
		#if ALARM_ENABLED
		server->on("/portal_alarms.js", HTTP_GET, handlePortalAlarmsJS);
		#endif
			 #if HAS_REMOTE_LOG
		server->on("/portal-logs.js", HTTP_GET, handlePortalLogsJS);
		#endif
		#if HAS_CAMERA
		server->on("/portal-camera.js", HTTP_GET, handlePortalCameraJS);
		server->on("/portal-camera.css", HTTP_GET, handlePortalCameraCSS);
		#endif
		#if HAS_LVGL_EPAPER
		server->on("/portal-epaper-refresh.js", HTTP_GET, handlePortalEpaperRefreshJS);
		#endif
		server->on("/portal-all.css", HTTP_GET, handlePortalAllCSS);

		// API endpoints
			 #if HAS_REMOTE_LOG
		#if DEBUG_CRASH_API_ENABLED
		server->on("/api/debug/crash", HTTP_POST, handleDebugCrash);
		#endif
		server->on("/api/logs/crash/download", HTTP_GET, handleDownloadCrashLog);
		server->on("/api/logs/crash", HTTP_GET, handleGetCrashLog);
		server->on("/api/logs", HTTP_GET, handleGetLogs);
			 #endif
		// NOTE: Keep more specific routes registered before more general/prefix routes.
		// Some AsyncWebServer matchers can behave like prefix matches depending on configuration.
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

		server->on("/api/info", HTTP_GET, handleGetVersion);
		server->on("/api/bindings", HTTP_GET, handleGetBindings);
		#if HEALTH_HISTORY_ENABLED
		server->on("/api/health/history", HTTP_GET, handleGetHealthHistory);
		#endif
		server->on("/api/health", HTTP_GET, handleGetHealth);

		server->on("/api/reboot", HTTP_POST, handleReboot);

		// GitHub Pages-based firmware updates (URL-driven)
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

#if HAS_DISPLAY
		server->on("/api/recipes/catalog", HTTP_GET, handleGetRecipeCatalog);
		server->on(
				"/api/recipes/catalog",
				HTTP_POST,
				[](AsyncWebServerRequest *request) {
						if (!portal_auth_gate(request)) return;
				},
				NULL,
				handlePostRecipeCatalog
		);

		// Pad button sizes (registered before /api/pad to avoid prefix match)
		server->on("/api/pad/button_sizes", HTTP_GET, handleGetButtonSizes);

		// Pad building blocks catalog (registered before /api/pad to avoid prefix match)
		server->on("/api/pad/blocks", HTTP_GET, handleGetPadBlocks);

#if HAS_MQTT
		// Pad binding live-resolve (registered before /api/pad to avoid prefix match)
		server->on(
				"/api/pad/resolve",
				HTTP_POST,
				[](AsyncWebServerRequest *request) {},
				NULL,
				handlePostPadResolve
		);
#endif

		// Pad config API
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



		server->on("/api/screenshot", HTTP_GET, handleGetScreenshot);
		#if HAS_TOUCH
		server->on("/api/screen/tap", HTTP_POST, handlePostScreenTap);
		#endif

		server->on(
				"/api/icons/install",
				HTTP_POST,
				[](AsyncWebServerRequest *request) {
						if (!portal_auth_gate(request)) return;
				},
				NULL,
				handlePostIconInstall
		);

		server->on("/api/icons/page", HTTP_DELETE, handleDeletePageIcons);

		server->on("/api/icons/installed", HTTP_GET, handleGetInstalledIcons);

		// Icon debug endpoints
		server->on("/api/icons/files", HTTP_GET, handleGetIconFiles);

		server->on("/api/icons/cache", HTTP_GET, handleGetIconCache);

		server->on("/api/icons/file", HTTP_GET, handleGetIconFile);
		server->on("/api/icons/file", HTTP_DELETE, handleDeleteIconFile);

#endif

#if HAS_CAMERA
		server->on("/api/camera/snapshot.raw", HTTP_GET, handleGetCameraRawSnapshot);

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

#if HAS_BLE_HID
		// BLE pairing endpoint
		server->on("/api/ble/pairing/start", HTTP_POST, [](AsyncWebServerRequest *request) {
				if (!portal_auth_gate(request)) return;
				handlePostBlePairingStart(request);
		});
#endif

#if HAS_TTF_FONTS
	server->on("/portal_fonts.js", HTTP_GET, handlePortalFontsJS);
	server->on("/api/fonts/list", HTTP_GET, handleGetFonts);
	server->on("/api/fonts", HTTP_DELETE, handleDeleteFont);
	server->on("/api/fonts/upload", HTTP_POST, handleFontUploadComplete, nullptr, handleFontUploadBody);
#endif
#if HAS_SOUND_PLAYER
		// Sound file management endpoints
		server->on(
				"/api/sounds/upload",
				HTTP_POST,
				[](AsyncWebServerRequest *request) {
						if (!portal_auth_gate(request)) return;
				},
				NULL,
				handlePostSoundUpload
		);

		server->on("/api/sounds/list", HTTP_GET, handleGetSoundList);

		server->on("/api/sounds", HTTP_DELETE, handleDeleteSound);

		server->on("/api/sounds/play", HTTP_POST, handlePostSoundPlay);
#endif

#if HAS_BOOT_LOGO
		server->on("/api/boot-logo/thumbnail", HTTP_GET, handleGetBootLogoThumbnail);
		server->on("/api/boot-logo", HTTP_GET, handleGetBootLogo);
		server->on("/api/boot-logo", HTTP_DELETE, handleDeleteBootLogo);
		server->on("/api/boot-logo/upload", HTTP_POST,
				handlePostBootLogoUploadComplete, nullptr, handlePostBootLogoUpload);
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
