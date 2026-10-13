#include "board_config.h"
#if IS_HEART_MONITOR
#include "../heart_catalog.h"
#include "../heart_recorder.h"
#include "component_registry.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"
#include "web_portal_routes.h"
#include "web_portal_path.h"
#include "web_portal_utils.h"
#include "ota_activity.h"
#include "storage.h"

REGISTER_NAV_COMPONENT(heart_sessions, "heart-sessions", "sensors", "Heart Sessions", 10, "heart-sessions")

namespace {
bool heart_session_busy() {
    const heart_monitor::Snapshot snapshot = heart_recorder_snapshot();
    return ota_activity_is_active() || snapshot.retained || snapshot.state == heart_monitor::State::Recording ||
        snapshot.state == heart_monitor::State::Saving || snapshot.state == heart_monitor::State::AwaitingSave;
}
void heart_sessions_register(AsyncWebServer* server) {
    server->on(AsyncURIMatcher::exact("/api/heart/sessions"), HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!portal_auth_gate(request)) return;
        const String list = heart_monitor::catalog_list();
        if (list.isEmpty()) request->send(503, "application/json", "{\"message\":\"Session catalog unavailable\"}");
        else request->send(200, "application/json", list);
    });
    server->on(AsyncURIMatcher::exact("/api/heart/sessions/import"), HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!portal_auth_gate(request)) return;
        if (heart_session_busy() || !heart_monitor::catalog_request_import()) {
            request->send(409, "application/json", "{\"message\":\"Capture, OTA or import is busy\"}");
            return;
        }
        request->send(202, "application/json", "{\"success\":true}");
    });
    server->on(AsyncURIMatcher::prefix("/api/heart/sessions/"), HTTP_GET | HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!portal_auth_gate(request)) return;
        const String id = portal_path_arg(request->url(), "/api/heart/sessions/", 0);
        bool valid = id.length() == 8;
        for (size_t index = 0; index < id.length(); ++index)
            valid = valid && ((id[index] >= '0' && id[index] <= '9') || (id[index] >= 'a' && id[index] <= 'f'));
        const bool item = portal_path_matches(request->url().c_str(), "/api/heart/sessions/", 1);
        const bool csv = portal_path_matches(request->url().c_str(), "/api/heart/sessions/", 2) &&
            portal_path_arg(request->url(), "/api/heart/sessions/", 1) == "raw.csv";
        if (!valid || (!item && !csv) || (request->method() == HTTP_DELETE && !item)) {
            request->send(400, "application/json", "{\"message\":\"Invalid session path\"}");
            return;
        }
        if (request->method() == HTTP_DELETE) {
            if (heart_session_busy()) {
                request->send(409, "application/json", "{\"message\":\"Capture or OTA is busy\"}");
                return;
            }
            const bool removed = heart_monitor::catalog_remove(id.c_str());
            request->send(removed ? 200 : 500, "application/json", removed ? "{\"success\":true}" : "{\"message\":\"Delete failed; retry to finish cleanup\"}");
            return;
        }
        char path[96];
        snprintf(path, sizeof(path), "/storage/heart/capture-%s.%s", id.c_str(), csv ? "csv" : "json");
        if (!Storage.exists(path)) {
            request->send(404, "application/json", "{\"message\":\"Session file not found\"}");
            return;
        }
        sendFileThrottled(request, path, csv ? "text/csv" : "application/json");
    });
    server->on(AsyncURIMatcher::prefix("/api/heart/sessions"), HTTP_OPTIONS, [](AsyncWebServerRequest* request) {
        web_portal_send_cors_preflight(request);
    });
}
}
REGISTER_ROUTES(heart_sessions_register);
#endif