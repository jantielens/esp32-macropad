// E-Paper VCOM portal component.
// Read / program / preview the panel's calibration voltage. The TPS65186
// EEPROM is rated for ~100,000 program cycles — the UI confirms before POST.
// Test pattern paints 8 grayscale bars + the current VCOM value at the top so
// the user can visually pick the cleanest setting.

#include "board_config.h"

#if HAS_EPAPER

#include "component_registry.h"
#include "device_classes/epaper/epaper_driver.h"
#include "log_manager.h"
#include "web_portal_auth.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>

static void epaper_vcom_get(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    if (!epaper_driver_begin()) {
        request->send(500, "application/json",
                      "{\"success\":false,\"message\":\"Panel init failed\"}");
        return;
    }
    const float v = epaper_driver_read_vcom();
    StaticJsonDocument<128> resp;
    resp["success"] = true;
    if (isnan(v)) {
        resp["vcom"] = nullptr;
    } else {
        resp["vcom"] = v;
    }
    String body;
    serializeJson(resp, body);
    request->send(200, "application/json", body);
}

static void epaper_vcom_post(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    if (!request->hasParam("value", true) && !request->hasParam("value")) {
        request->send(400, "application/json",
                      "{\"success\":false,\"message\":\"Missing 'value' parameter\"}");
        return;
    }
    const String s = request->hasParam("value", true)
        ? request->getParam("value", true)->value()
        : request->getParam("value")->value();
    const float v = s.toFloat();
    if (!(v < 0.0f && v >= -5.0f)) {
        request->send(400, "application/json",
                      "{\"success\":false,\"message\":\"VCOM must be in range [-5.0, 0.0)\"}");
        return;
    }
    LOGI("API", "POST /api/component/epaper-vcom/vcom value=%.3f", v);
    if (!epaper_driver_begin()) {
        request->send(500, "application/json",
                      "{\"success\":false,\"message\":\"Panel init failed\"}");
        return;
    }
    const bool ok = epaper_driver_write_vcom(v);
    StaticJsonDocument<128> resp;
    resp["success"] = ok;
    resp["vcom"] = v;
    if (!ok) resp["message"] = "VCOM program failed (see serial log)";
    String body;
    serializeJson(resp, body);
    request->send(ok ? 200 : 500, "application/json", body);
}

static void epaper_vcom_test_pattern_post(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    // Optional ?vcom=-X.Y query param previews that value via the TPS65186
    // volatile registers without burning an EEPROM write. Range matches
    // epaper_driver_write_vcom(): -5.0 .. <0.0 V.
    float preview = NAN;
    if (request->hasParam("vcom")) {
        const float v = request->getParam("vcom")->value().toFloat();
        if (v < 0.0f && v >= -5.0f) {
            preview = v;
        } else {
            request->send(400, "application/json",
                          "{\"success\":false,\"message\":\"vcom must be in range [-5.0, 0.0)\"}");
            return;
        }
    }
    LOGI("API", "POST /api/component/epaper-vcom/vcom-test-pattern preview=%.3f", preview);
    if (!epaper_driver_begin()) {
        request->send(500, "application/json",
                      "{\"success\":false,\"message\":\"Panel init failed\"}");
        return;
    }
    epaper_driver_show_vcom_test_pattern(preview);
    request->send(200, "application/json", "{\"success\":true}");
}

static const ComponentAction epaper_vcom_actions[] = {
    {"vcom",              HTTP_GET,  epaper_vcom_get,               nullptr},
    {"vcom",              HTTP_POST, epaper_vcom_post,              nullptr},
    {"vcom-test-pattern", HTTP_POST, epaper_vcom_test_pattern_post, nullptr},
};

static ComponentDef epaper_vcom_component = {
    .id = "epaper-vcom",
    .category = "epaper",
    .display_name = "VCOM",
    .nav_order = 40,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = epaper_vcom_actions,
    .num_custom_actions = sizeof(epaper_vcom_actions) / sizeof(epaper_vcom_actions[0]),
    .fragment_id = "epaper-vcom",
};

REGISTER_COMPONENT(epaper_vcom);

#endif // HAS_EPAPER
