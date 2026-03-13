#include "web_portal_routes.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"
#include "web_portal_config.h"
#include "web_portal_device_api.h"
#include "web_portal_display.h"
#include "web_portal_firmware.h"
#include "web_portal_icons.h"
#include "web_portal_ota.h"
#include "web_portal_pad.h"
#include "web_portal_pages.h"

#include "board_config.h"

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
		server->on("/portal.css", HTTP_GET, handleCSS);
		server->on("/portal.js", HTTP_GET, handleJS);

		// Split JS module routes
		server->on("/portal_core.js", HTTP_GET, handleCoreJS);
		server->on("/portal_config.js", HTTP_GET, handleConfigJS);
		server->on("/portal_firmware.js", HTTP_GET, handleFirmwareJS);
		server->on("/portal_health.js", HTTP_GET, handleHealthJS);
		server->on("/portal_pad_colors.js", HTTP_GET, handlePadColorsJS);
		server->on("/portal_pad_io.js", HTTP_GET, handlePadIOJS);
		server->on("/portal_pad_editor.js", HTTP_GET, handlePadEditorJS);

		// API endpoints
		// NOTE: Keep more specific routes registered before more general/prefix routes.
		// Some AsyncWebServer matchers can behave like prefix matches depending on configuration.
		registerOptions("/api/mode");
		server->on("/api/mode", HTTP_GET, handleGetMode);

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
		// Display API endpoints
		server->on(
				"/api/display/brightness",
				HTTP_PUT,
				[](AsyncWebServerRequest *request) {
						if (!portal_auth_gate(request)) return;
				},
				NULL,
				handleSetDisplayBrightness
		);
		registerOptions("/api/display/brightness");

		// Screen saver API endpoints
		registerOptions("/api/display/sleep");
		server->on("/api/display/sleep", HTTP_GET, handleGetDisplaySleep);
		server->on("/api/display/sleep", HTTP_POST, handlePostDisplaySleep);

		registerOptions("/api/display/wake");
		server->on("/api/display/wake", HTTP_POST, handlePostDisplayWake);

		registerOptions("/api/display/activity");
		server->on("/api/display/activity", HTTP_POST, handlePostDisplayActivity);

		// Runtime-only screen switch
		server->on(
				"/api/display/screen",
				HTTP_PUT,
				[](AsyncWebServerRequest *request) {
						if (!portal_auth_gate(request)) return;
				},
				NULL,
				handleSetDisplayScreen
		);
		registerOptions("/api/display/screen");

		// Pad tile sizes (registered before /api/pad to avoid prefix match)
		registerOptions("/api/pad/tile_sizes");
		server->on("/api/pad/tile_sizes", HTTP_GET, handleGetTileSizes);

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

		// Icon store API
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

}
