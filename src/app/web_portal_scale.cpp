#include "web_portal_scale.h"
#include "board_config.h"

#if HAS_SCALE

#include "scale_hal.h"
#include "config_manager.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"
#include "web_portal_state.h"
#include "log_manager.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#define TAG "WebScale"

extern DeviceConfig device_config;

// POST /api/scale/tare — zero the scale
void handlePostScaleTare(AsyncWebServerRequest* request) {
    LOGI(TAG, "Tare requested via portal");
    scale_tare();

    // Persist the new offset to NVS
    snprintf(device_config.scale_offset, CONFIG_SCALE_CAL_MAX_LEN, "%ld", scale_get_offset());
    config_manager_save(&device_config);

    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", "{\"ok\":true}");
    web_portal_add_cors_headers(response);
    request->send(response);
}

// POST /api/scale/calibrate — body: { "known_weight_g": 100.0 }
void handlePostScaleCalibrate(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    // Only process once all data received
    if (index + len < total) return;

    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, (const char*)data, len);
    if (err || !doc.containsKey("known_weight_g")) {
        AsyncWebServerResponse* response = request->beginResponse(400, "application/json",
            "{\"ok\":false,\"error\":\"Expected JSON with known_weight_g\"}");
        web_portal_add_cors_headers(response);
        request->send(response);
        return;
    }

    float known_weight = doc["known_weight_g"].as<float>();
    if (known_weight <= 0.0f) {
        AsyncWebServerResponse* response = request->beginResponse(400, "application/json",
            "{\"ok\":false,\"error\":\"known_weight_g must be positive\"}");
        web_portal_add_cors_headers(response);
        request->send(response);
        return;
    }

    LOGI(TAG, "Calibrate requested: known_weight=%.1f g", known_weight);

    // Temporarily set cal_weight to the requested value, calibrate, then restore
    float saved_cal_weight = scale_get_cal_weight();
    scale_adjust_cal_weight(known_weight - saved_cal_weight);  // set to known_weight
    float factor = scale_calibrate_with_cal_weight();
    scale_adjust_cal_weight(saved_cal_weight - known_weight);  // restore

    if (factor == 0.0f) {
        AsyncWebServerResponse* response = request->beginResponse(400, "application/json",
            "{\"ok\":false,\"error\":\"Raw delta is zero — no load cell signal. Check wiring.\"}");
        web_portal_add_cors_headers(response);
        request->send(response);
        return;
    }

    // Persist calibration to NVS
    snprintf(device_config.scale_cal_factor, CONFIG_SCALE_CAL_MAX_LEN, "%.4f", scale_get_calibration_factor());
    snprintf(device_config.scale_offset, CONFIG_SCALE_CAL_MAX_LEN, "%ld", scale_get_offset());
    config_manager_save(&device_config);

    char resp[128];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"calibration_factor\":%.4f}", scale_get_calibration_factor());
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", resp);
    web_portal_add_cors_headers(response);
    request->send(response);
}

// GET /api/scale — current scale state
void handleGetScaleStatus(AsyncWebServerRequest* request) {
    char resp[192];
    snprintf(resp, sizeof(resp),
        "{\"available\":%s,\"weight_g\":%.1f,\"flow_rate\":%.1f,\"calibration_factor\":%.4f,\"offset\":%ld}",
        scale_is_available() ? "true" : "false",
        scale_get_weight(),
        scale_get_flow_rate(),
        scale_get_calibration_factor(),
        scale_get_offset());
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", resp);
    web_portal_add_cors_headers(response);
    request->send(response);
}

void web_portal_scale_register_routes(AsyncWebServer* server) {
    server->on("/api/scale/tare", HTTP_OPTIONS, [](AsyncWebServerRequest *r){ web_portal_send_cors_preflight(r); });
    server->on("/api/scale/tare", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!portal_auth_gate(request)) return;
        handlePostScaleTare(request);
    });

    server->on("/api/scale/calibrate", HTTP_OPTIONS, [](AsyncWebServerRequest *r){ web_portal_send_cors_preflight(r); });
    server->on(
        "/api/scale/calibrate",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!portal_auth_gate(request)) return;
        },
        NULL,
        handlePostScaleCalibrate
    );

    server->on("/api/scale", HTTP_OPTIONS, [](AsyncWebServerRequest *r){ web_portal_send_cors_preflight(r); });
    server->on("/api/scale", HTTP_GET, handleGetScaleStatus);
}

#endif // HAS_SCALE
