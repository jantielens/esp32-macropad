// E-Paper portal component.
// Nav entry + custom "refresh" action that triggers an immediate redraw.

#include "board_config.h"

#if HAS_EPAPER

#include "component_registry.h"
#include "config_manager.h"
#include "epaper_refresh.h"
#include "log_manager.h"
#include "power_manager.h"
#include "web_portal_auth.h"
#include "web_portal_state.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>

static void epaper_refresh_post(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;

    DeviceConfig* cfg = web_portal_get_current_config();
    if (!cfg) {
        request->send(500, "application/json",
                      "{\"success\":false,\"message\":\"Config not loaded\"}");
        return;
    }

    if (strlen(cfg->epaper_url) == 0) {
        request->send(400, "application/json",
                      "{\"success\":false,\"message\":\"No image URL configured\"}");
        return;
    }

    if (!WiFi.isConnected()) {
        request->send(503, "application/json",
                      "{\"success\":false,\"message\":\"WiFi not connected\"}");
        return;
    }

    LOGI("API", "POST /api/component/epaper/refresh");
    EpaperRefreshOutcome out = epaper_refresh_run(cfg, true /*force*/);

    StaticJsonDocument<256> resp;
    resp["success"] = (out.result == EpaperRefreshResult::Updated ||
                       out.result == EpaperRefreshResult::Skipped);
    switch (out.result) {
        case EpaperRefreshResult::Updated:     resp["result"] = "updated";      break;
        case EpaperRefreshResult::Skipped:     resp["result"] = "skipped";      break;
        case EpaperRefreshResult::FailedFetch: resp["result"] = "fetch_failed"; break;
        case EpaperRefreshResult::FailedDraw:  resp["result"] = "draw_failed";  break;
        case EpaperRefreshResult::Disabled:    resp["result"] = "disabled";     break;
    }
    resp["crc"] = out.crc_used;
    resp["sidecar_http_status"] = out.sidecar_http_status;
    resp["battery_mv"] = out.battery_mv;
    resp["elapsed_ms"] = out.elapsed_ms;

    String body;
    serializeJson(resp, body);
    request->send(200, "application/json", body);
}

static void epaper_status_get(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;

    const DeviceConfig* cfg = web_portal_get_current_config();
    const uint32_t last_unix = epaper_refresh_last_unix();
    const EpaperRefreshOutcome last = epaper_refresh_last_outcome();
    const time_t now = time(nullptr);
    // Match the threshold used in epaper_refresh.cpp (2024-01-01).
    const bool clock_synced = (now >= (time_t)1704067200);

    StaticJsonDocument<256> resp;
    if (last_unix == 0) {
        // No refresh recorded since cold boot (RTC memory was cleared by
        // power loss or this is the first ever boot).
        resp["last_refresh_seconds_ago"] = nullptr;
        resp["last_refresh_status"] = "never";
    } else if (!clock_synced) {
        // We have a previously-persisted timestamp but no current wall clock
        // to compute against. Surface the raw epoch so the UI can still show
        // something useful.
        resp["last_refresh_seconds_ago"] = nullptr;
        resp["last_refresh_status"] = "clock_unsynced";
    } else if ((uint32_t)now < last_unix) {
        // Clock went backwards (NTP correction, manual set). Treat as unknown.
        resp["last_refresh_seconds_ago"] = nullptr;
        resp["last_refresh_status"] = "clock_unsynced";
    } else {
        resp["last_refresh_seconds_ago"] = (uint32_t)now - last_unix;
        resp["last_refresh_status"] = "ok";
    }
    resp["last_refresh_unix"] = last_unix;
    resp["refresh_count"]   = epaper_refresh_get_count();
    switch (last.result) {
        case EpaperRefreshResult::Updated:     resp["last_result"] = "updated"; break;
        case EpaperRefreshResult::Skipped:     resp["last_result"] = "skipped"; break;
        case EpaperRefreshResult::FailedFetch: resp["last_result"] = "fetch_failed"; break;
        case EpaperRefreshResult::FailedDraw:  resp["last_result"] = "draw_failed"; break;
        case EpaperRefreshResult::Disabled:    resp["last_result"] = "disabled"; break;
    }
    resp["sidecar_http_status"] = last.sidecar_http_status;
    resp["battery_mv"]      = last.battery_mv;
    resp["last_crc32"]      = cfg ? cfg->epaper_last_crc32 : 0;
    resp["last_elapsed_ms"] = last.elapsed_ms;

    String body;
    serializeJson(resp, body);
    request->send(200, "application/json", body);
}

static const ComponentAction epaper_actions[] = {
    {"refresh", HTTP_POST, epaper_refresh_post, nullptr},
    {"status",  HTTP_GET,  epaper_status_get,   nullptr},
};

static ComponentDef epaper_component = {
    .id = "epaper",
    .category = "epaper",
    .display_name = "E-Paper",
    .nav_order = 10,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = epaper_actions,
    .num_custom_actions = sizeof(epaper_actions) / sizeof(epaper_actions[0]),
    .fragment_id = "epaper",
};

REGISTER_COMPONENT(epaper);

#endif // HAS_EPAPER
