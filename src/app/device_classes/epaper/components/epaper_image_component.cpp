// E-Paper Image & Schedule portal component.
// Config (image URL, rotation, wake interval, WiFi backoff, frontlight) is
// saved through the shared /api/config endpoint. This component declares the
// nav entry plus row-level actions for rapid image testing.

#include "board_config.h"

#if HAS_EPAPER

#include "component_registry.h"
#include "config_manager.h"
#include "device_classes/epaper/epaper_driver.h"
#include "device_classes/epaper/epaper_refresh.h"
#include "log_manager.h"
#include "main_loop_bridge.h"
#include "web_portal_auth.h"
#include "web_portal_state.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static void epaper_image_result_json(AsyncWebServerRequest* request, const EpaperRefreshOutcome& out) {
    StaticJsonDocument<192> resp;
    const bool success = (out.result == EpaperRefreshResult::Updated);
    resp["success"] = success;
    switch (out.result) {
        case EpaperRefreshResult::Updated:     resp["result"] = "updated";      break;
        case EpaperRefreshResult::Skipped:     resp["result"] = "skipped";      break;
        case EpaperRefreshResult::FailedFetch: resp["result"] = "fetch_failed"; break;
        case EpaperRefreshResult::FailedDraw:  resp["result"] = "draw_failed";  break;
        case EpaperRefreshResult::Disabled:    resp["result"] = "disabled";     break;
    }
    resp["battery_mv"] = out.battery_mv;
    resp["elapsed_ms"] = out.elapsed_ms;
    String body;
    serializeJson(resp, body);
    request->send(success ? 200 : 500, "application/json", body);
}

static void epaper_image_show_url_post(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;

    DeviceConfig* cfg = web_portal_get_current_config();
    if (!cfg) {
        request->send(500, "application/json",
                      "{\"success\":false,\"message\":\"Config not loaded\"}");
        return;
    }
    if (!request->hasParam("url", true) && !request->hasParam("url")) {
        request->send(400, "application/json",
                      "{\"success\":false,\"message\":\"Missing 'url' parameter\"}");
        return;
    }
    const String url = request->hasParam("url", true)
        ? request->getParam("url", true)->value()
        : request->getParam("url")->value();
    if (url.length() == 0) {
        request->send(400, "application/json",
                      "{\"success\":false,\"message\":\"Image URL is empty\"}");
        return;
    }
    if (!WiFi.isConnected()) {
        request->send(503, "application/json",
                      "{\"success\":false,\"message\":\"WiFi not connected\"}");
        return;
    }

    LOGI("API", "POST /api/component/epaper-image/show-url");
    EpaperRefreshOutcome out = epaper_refresh_show_url(cfg, url.c_str());
    epaper_image_result_json(request, out);
}

static void epaper_image_clear_cache_exec(const void*, bool* ok, char*, size_t) {
    *ok = epaper_driver_sd_cache_clear();
    LOGI("Epaper", "SD cache clear %s", *ok ? "complete" : "failed");
}

static void epaper_image_clear_cache_post(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    LOGI("API", "POST /api/component/epaper-image/clear-sd-cache");
    const LoopBridgeResult result = loop_bridge_enqueue(epaper_image_clear_cache_exec, nullptr, 0);
    if (result == LOOP_BRIDGE_BUSY) {
        request->send(409, "application/json",
                      "{\"success\":false,\"message\":\"Device is busy\"}");
        return;
    }
    if (result != LOOP_BRIDGE_OK) {
        request->send(503, "application/json",
                      "{\"success\":false,\"message\":\"Cache clear unavailable\"}");
        return;
    }
    request->send(202, "application/json",
                  "{\"success\":true,\"message\":\"SD cache clear started\"}");
}

static const ComponentAction epaper_image_actions[] = {
    {"show-url", HTTP_POST, epaper_image_show_url_post, nullptr},
    {"clear-sd-cache", HTTP_POST, epaper_image_clear_cache_post, nullptr},
};

static ComponentDef epaper_image_component = {
    .id = "epaper-image",
    .category = "epaper",
    .display_name = "Image & Schedule",
    .nav_order = 20,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = epaper_image_actions,
    .num_custom_actions = sizeof(epaper_image_actions) / sizeof(epaper_image_actions[0]),
    .fragment_id = "epaper-image",
};

REGISTER_COMPONENT(epaper_image);

#endif // HAS_EPAPER
